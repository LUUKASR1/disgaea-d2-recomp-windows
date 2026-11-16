#include "iso_vfs.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#endif

struct ProbeResult { int status; std::string report; };
static ProbeResult probe(const std::filesystem::path& image) {
    std::ostringstream report;
    report << "Disgaea D2 - ISO VFS check v3 (experimental)\n"
           << "Checks native UDF/ISO9660 reads only; does not launch the game.\n";
    auto name = image.u8string();
    report << "ISO: " << std::string(reinterpret_cast<const char*>(name.data()), name.size()) << "\n";
    try {
        auto layout = d2::vfs::InspectImage(image);
        report << "Image bytes=" << layout.bytes << " aligned_2048=" << layout.aligned << '\n'
               << "Format: ISO9660=" << layout.iso9660Primary << " UDF_VRS=" << layout.udfRecognition
               << " UDF_anchor=" << layout.udfAnchor << '\n';
        for (const auto& descriptor : layout.descriptors) report << descriptor << '\n';
        report << "Inspection: " << layout.Description() << '\n';
        d2::vfs::IsoVfs disc(image);
        report << "Mounted backend=" << disc.BackendName() << '\n';
        const auto title = disc.TitleId();
        if (title != "BLUS31313") throw std::runtime_error("Este port requer BLUS31313; ISO selecionada: " + title);
        report << "ISO filesystem OK; TITLE_ID=" << title << '\n';
        const auto check = [&](const std::string& path) {
            auto file = disc.Open(path);
            if (!file->Size()) throw std::runtime_error("Arquivo vazio: " + path);
            std::array<std::byte, 4096> bytes{};
            auto read = file->ReadAt(0, bytes);
            report << "OK " << path << " size=" << file->Size() << " sampled=" << read << '\n';
        };
        check("/PS3_GAME/PARAM.SFO"); check("/PS3_GAME/USRDIR/EBOOT.BIN");
        for (const auto* stem : {"START", "ANM_HI"}) {
            for (unsigned part = 0; part < 7; ++part)
                check(std::string("/PS3_GAME/USRDIR/Data/") + stem +
                    (part ? "_" + std::to_string(part) : "") + ".dat");
        }
        check("/PS3_GAME/USRDIR/Data/NisAnmShadow.txf");
        check("/PS3_GAME/USRDIR/Data/SOUND/SndPakBgm/SndPakBgm_0.pak");
        report << "18 required base files checked. Update/DLC files are separate.\n"
               << "PASS: required base files readable through " << disc.BackendName() << ".\n"
               << "Full game/asset decryption and runtime behavior still require validation.\n";
        return {0, report.str()};
    } catch (const std::exception& error) {
        report << "FAIL: " << error.what() << '\n'; return {1, report.str()};
    }
}


static int prepareMetadata(const std::filesystem::path& image, const std::filesystem::path& root) {
    const auto checked = probe(image);
    std::cout << checked.report;
    if (checked.status) return checked.status;
    try {
        d2::vfs::IsoVfs disc(image);
        if (disc.TitleId() != "BLUS31313") throw std::runtime_error("ISO alterada durante verificacao.");
        auto bytes = disc.GetFileBytes("/PS3_GAME/PARAM.SFO", 1024 * 1024);
        const auto folder = root / "PS3_GAME";
        const auto target = folder / "PARAM.SFO";
        if (std::filesystem::exists(target)) {
            std::ifstream input(target, std::ios::binary);
            std::string old((std::istreambuf_iterator<char>(input)), {});
            if (old.size() != bytes.size() || !std::equal(bytes.begin(), bytes.end(),
                reinterpret_cast<const std::byte*>(old.data())))
                throw std::runtime_error("Metadados existentes diferentes; arquivos preservados.");
            return 0;
        }
        std::filesystem::create_directories(folder);
        const auto temporary = folder / "PARAM.SFO.pending";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); output.close();
        if (!output) throw std::runtime_error("Falha ao salvar metadados ISO.");
        std::filesystem::rename(temporary, target);
        std::cout << "Metadata-only cache ready; no game assets or executable extracted.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#ifdef _WIN32
static std::filesystem::path saveReport(const std::string& report) {
    PWSTR downloads = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads)))
        throw std::runtime_error("Nao foi possivel localizar Downloads.");
    std::filesystem::path folder(downloads); CoTaskMemFree(downloads);
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t name[96];
    swprintf(name, 96, L"DisgaeaD2-ISO-diagnostico-%04u%02u%02u-%02u%02u%02u-%03u.txt",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    const auto path = folder / name;
    std::ofstream output(path, std::ios::binary);
    output << report; output.close();
    if (!output) throw std::runtime_error("Nao foi possivel salvar o relatorio em Downloads.");
    return path;
}
int main() {
    int count = 0; wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    if (count == 4 && std::wstring_view(args[1]) == L"--metadata-root") {
        auto root = std::filesystem::path(args[2]); auto image = std::filesystem::path(args[3]);
        LocalFree(args); return prepareMetadata(image, root);
    }
    const bool interactive = count == 1;
    std::filesystem::path image;
    if (interactive) {
        std::array<wchar_t, 32768> selected{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"ISO do PS3 descriptografada (*.iso)\0*.iso\0Todos os arquivos\0*.*\0";
        dialog.lpstrFile = selected.data(); dialog.nMaxFile = static_cast<DWORD>(selected.size());
        dialog.lpstrTitle = L"Disgaea D2 - verificar ISO UDF / ISO9660";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (!GetOpenFileNameW(&dialog)) { LocalFree(args); return CommDlgExtendedError() ? 2 : 0; }
        image = selected.data();
    } else if (count == 2) image = args[1];
    else { LocalFree(args); std::cerr << "Uso: DisgaeaD2-ISO-Check.exe <ISO descriptografada>\n"; return 2; }
    LocalFree(args);
    const auto result = probe(image);
    std::cout << result.report;
    try {
        const auto report = saveReport(result.report);
        std::wstring message = result.status == 0
            ? L"Leitura dos arquivos base aprovada.\nEste diagnostico ainda nao executa o jogo.\n\nRelatorio: "
            : L"A ISO nao passou na verificacao de arquivos.\nConfira o backend utilizado e o motivo no relatorio.\n\nRelatorio: ";
        message += report.wstring();
        if (interactive) MessageBoxW(nullptr, message.c_str(), L"Disgaea D2 - ISO Check",
            MB_OK | (result.status ? MB_ICONWARNING : MB_ICONINFORMATION));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (interactive) MessageBoxW(nullptr, L"Nao foi possivel salvar o relatorio em Downloads.",
            L"Disgaea D2 - ISO Check", MB_OK | MB_ICONERROR);
        return 2;
    }
    return result.status;
}
#else
int main(int count, char** args) {
    if (count == 4 && std::string_view(args[1]) == "--metadata-root")
        return prepareMetadata(args[3], args[2]);
    if (count != 2) { std::cerr << "Uso: d2_iso_probe <ISO descriptografada>\n"; return 2; }
    const auto result = probe(args[1]); std::cout << result.report; return result.status;
}
#endif
