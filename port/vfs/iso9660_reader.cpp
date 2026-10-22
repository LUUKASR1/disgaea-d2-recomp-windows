#include "iso9660_reader.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <unordered_map>

namespace d2::vfs::detail {
namespace {
constexpr std::uint64_t Block = 2048;
[[noreturn]] void invalid(const char* message) { throw Error(ErrorCode::InvalidImage, message); }
std::uint32_t dual(std::span<const std::uint8_t> bytes, std::size_t at, unsigned width) {
    if (at > bytes.size() || 2 * width > bytes.size() - at) invalid("Registro ISO9660 truncado.");
    std::uint32_t little = 0, big = 0;
    for (unsigned i = 0; i < width; ++i) {
        little |= std::uint32_t(bytes[at + i]) << (8 * i);
        big = (big << 8) | bytes[at + width + i];
    }
    if (little != big) invalid("Campos ISO9660 little/big-endian inconsistentes.");
    return little;
}
}

std::string Iso9660Reader::Fold(std::string_view name) {
    std::string folded(name);
    for (char& c : folded) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return folded;
}
Iso9660Reader::Iso9660Reader(Read read, std::uint64_t bytes)
    : read_(std::move(read)), bytes_(bytes) {
    std::array<std::uint8_t, Block> block{};
    for (std::uint64_t lba = 16; lba < 64 && lba < bytes / Block; ++lba) {
        if (!read_(lba * Block, block.data(), block.size())) invalid("Falha ao ler PVD ISO9660.");
        if (std::memcmp(block.data() + 1, "CD001", 5) || block[6] != 1) continue;
        if (block[0] == 255) break;
        if (block[0] != 1) continue;
        if (dual(block, 128, 2) != Block) invalid("ISO9660 requer blocos de 2048 bytes.");
        const auto volumeBlocks = dual(block, 80, 4);
        if (!volumeBlocks || std::uint64_t(volumeBlocks) * Block > bytes_)
            invalid("Tamanho do volume ISO9660 excede a imagem.");
        volumeBytes_ = std::uint64_t(volumeBlocks) * Block;
        sequence_ = dual(block, 124, 2);
        if (!sequence_ || dual(block, 120, 2) != 1)
            invalid("Volumes ISO9660 divididos em varios discos nao suportados.");
        auto record = Decode(std::span<const std::uint8_t>(block).subspan(156));
        if (!record.node.directory || record.more || record.identifier != std::string(1, '\0'))
            invalid("Diretorio raiz ISO9660 invalido.");
        root_ = std::move(record.node); root_.name = "/";
        Directory(root_); // Validate the actual root records before mounting.
        return;
    }
    invalid("Descritor primario ISO9660 nao encontrado.");
}

Iso9660Reader::Record Iso9660Reader::Decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 34 || bytes[0] < 34 || bytes[0] > bytes.size()) invalid("Registro de diretorio ISO9660 invalido.");
    bytes = bytes.first(bytes[0]);
    const auto nameLength = bytes[32];
    if (!nameLength || nameLength > bytes.size() - 33) invalid("Nome ISO9660 truncado.");
    if (bytes[26] || bytes[27]) invalid("Arquivos ISO9660 intercalados nao suportados.");
    if (dual(bytes, 28, 2) != sequence_) invalid("Registro ISO9660 aponta para outro volume.");
    Record record{};
    record.node.directory = (bytes[25] & 2) != 0;
    record.more = (bytes[25] & 128) != 0;
    if (record.node.directory && record.more) invalid("Diretorio ISO9660 com multiplas secoes nao suportado.");
    const auto block = dual(bytes, 2, 4);
    record.node.size = dual(bytes, 10, 4);
    if (record.more && (!record.node.size || record.node.size % Block))
        invalid("Secao ISO9660 intermediaria deve ocupar blocos completos.");
    // An Extended Attribute Record occupies complete blocks before file data.
    const auto offset = (std::uint64_t(block) + bytes[1]) * Block;
    if (offset > volumeBytes_ || record.node.size > volumeBytes_ - offset)
        invalid("Extent ISO9660 excede os limites do volume.");
    record.node.extents.push_back({offset, record.node.size});
    record.special = nameLength == 1 && (bytes[33] == 0 || bytes[33] == 1);
    record.identifier.assign(reinterpret_cast<const char*>(bytes.data() + 33), nameLength);
    if (record.special) return record;
    for (unsigned i = 0; i < nameLength; ++i)
        if (bytes[33 + i] < 32 || bytes[33 + i] > 126 || bytes[33 + i] == '/' || bytes[33 + i] == '\\')
            invalid("Identificador ISO9660 nao suportado pelo perfil ASCII.");
    record.node.name = record.identifier;
    if (!record.node.directory) {
        const auto delimiter = record.node.name.rfind(';');
        if (delimiter != std::string::npos) {
            const auto* start = record.node.name.data() + delimiter + 1;
            const auto* end = record.node.name.data() + record.node.name.size();
            auto result = std::from_chars(start, end, record.node.version);
            if (result.ec != std::errc{} || result.ptr != end || !record.node.version)
                invalid("Versao de identificador ISO9660 invalida.");
            record.node.name.resize(delimiter);
        }
        if (!record.node.name.empty() && record.node.name.back() == '.') record.node.name.pop_back();
    }
    if (record.node.name.empty() || record.node.name == "." || record.node.name == "..")
        invalid("Nome ISO9660 invalido.");
    return record;
}

const std::vector<IsoNode>& Iso9660Reader::Directory(const IsoNode& node) {
    if (!node.directory) throw Error(ErrorCode::NotDirectory, "Caminho ISO9660 nao e um diretorio.");
    if (node.extents.size() != 1 || node.size > 16u * 1024u * 1024u)
        invalid("Diretorio ISO9660 excede o perfil suportado.");
    const auto key = std::to_string(node.extents[0].offset) + ":" + std::to_string(node.size);
    for (auto it = directories_.begin(); it != directories_.end(); ++it)
        if (it->key == key) { directories_.splice(directories_.begin(), directories_, it); return directories_.front().entries; }
    std::vector<IsoNode> entries;
    std::unordered_map<std::string, std::size_t> names;
    std::string pendingIdentifier;
    IsoNode pending;
    bool more = false;
    const auto append = [&](IsoNode entry) {
        const auto name = Fold(entry.name);
        if (const auto found = names.find(name); found != names.end()) {
            auto& existing = entries[found->second];
            if (entry.directory || existing.directory || entry.version == existing.version)
                invalid("Identificadores ISO9660 ambiguos.");
            if (entry.version > existing.version) existing = std::move(entry);
        } else {
            if (entries.size() >= 10000) invalid("Diretorio ISO9660 excede limite de entradas.");
            names.emplace(name, entries.size()); entries.push_back(std::move(entry));
        }
    };
    std::array<std::uint8_t, Block> block{};
    for (std::uint64_t position = 0; position < node.size; position += Block) {
        const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(Block, node.size - position));
        if (!read_(node.extents[0].offset + position, block.data(), length)) invalid("Erro ao ler diretorio ISO9660.");
        for (std::size_t at = 0; at < length;) {
            if (!block[at]) break; // Padding to the next logical sector.
            auto record = Decode(std::span<const std::uint8_t>(block).subspan(at, length - at));
            at += block[at];
            if (record.special) { if (more) invalid("Sequencia ISO9660 interrompida."); continue; }
            if (more) {
                if (record.identifier != pendingIdentifier || record.node.directory)
                    invalid("Secoes ISO9660 nao consecutivas ou inconsistentes.");
                if (record.node.size > UINT64_MAX - pending.size) invalid("Tamanho ISO9660 excede limite.");
                pending.size += record.node.size;
                if (pending.extents.size() >= 4096) invalid("Muitas secoes ISO9660.");
                pending.extents.push_back(record.node.extents[0]);
                more = record.more;
                if (!more) append(std::move(pending));
            } else if (record.more) {
                more = true; pendingIdentifier = record.identifier; pending = std::move(record.node);
            } else append(std::move(record.node));
        }
    }
    if (more) invalid("Arquivo ISO9660 tem sequencia incompleta de secoes.");
    if (directories_.size() >= 32) directories_.pop_back();
    directories_.push_front({key, std::move(entries)});
    return directories_.front().entries;
}

IsoNode Iso9660Reader::Resolve(const std::string& path) {
    auto node = root_;
    if (path == "/") return node;
    for (std::size_t start = 1; start < path.size();) {
        const auto end = path.find('/', start);
        const auto wanted = Fold(std::string_view(path).substr(start,
            end == path.npos ? path.size() - start : end - start));
        const auto& entries = Directory(node);
        const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& candidate) { return Fold(candidate.name) == wanted; });
        if (entry == entries.end()) throw Error(ErrorCode::NotFound, "Arquivo ISO9660 nao encontrado: " + path);
        node = *entry;
        if (end == path.npos) break;
        start = end + 1;
    }
    return node;
}

std::size_t Iso9660Reader::ReadAt(const IsoNode& node, std::uint64_t offset, std::span<std::byte> output) {
    if (node.directory) throw Error(ErrorCode::IsDirectory, "Leitura de diretorio como arquivo.");
    if (offset >= node.size || output.empty()) return 0;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), node.size - offset));
    std::size_t done = 0;
    for (const auto& extent : node.extents) {
        if (offset >= extent.length) { offset -= extent.length; continue; }
        const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(count - done, extent.length - offset));
        if (!read_(extent.offset + offset, output.data() + done, length))
            throw Error(ErrorCode::Io, "Falha de leitura dos dados ISO9660.");
        done += length; offset = 0;
        if (done == count) return done;
    }
    throw Error(ErrorCode::Io, "Extents ISO9660 nao cobrem o tamanho do arquivo.");
}
}
