# Inicialização Win32 e correspondências DX12 → Vulkan

O exemplo completo está em `win32_init.h`. Recebe a janela existente, habilita obrigatoriamente `VK_KHR_surface` e `VK_KHR_win32_surface` no `VkInstance`, cria `VkSurfaceKHR`, seleciona um adaptador e uma família com graphics + present na superfície e cria `VkDevice` com `VK_KHR_swapchain`. Prefere GPU discreta. Não cria swapchain, comandos ou pipelines do jogo. A CI Windows apenas compila o exemplo; o renderer separado já possui apresentação e resize.

```cpp
#include "win32_init.h"

d2::examples::Win32VulkanContext graphics; // Vive enquanto o renderer existir.
// hwnd e hinstance pertencem à janela Win32 já criada pelo seu programa.
graphics.Initialize(hwnd, hinstance);
VkDevice device = graphics.device;
VkQueue queue = graphics.graphicsQueue;
// Crie os recursos do renderer com device/queue e graphics.surface.
// Ao encerrar: espere trabalhos pendentes, destrua os recursos filhos,
// depois destrua graphics e por último a janela.
```

Para desenvolver esse exemplo separado, disponibilize os headers Vulkan e vincule `vulkan-1.lib`. O usuário final precisa do loader fornecido pelo driver, não do Vulkan SDK. No projeto, o renderer usa carregamento dinâmico de `vulkan-1.dll`; o exemplo usa protótipos/import library por simplicidade. A seleção exige uma família comum graphics/present; dispositivos que precisem de duas famílias são rejeitados explicitamente.

| DirectX 12 / DXGI | Vulkan | Observação |
|---|---|---|
| `IDXGIFactory` | `VkInstance` + enumeração física | Correspondência funcional, não herança de objetos. |
| `IDXGIAdapter` | `VkPhysicalDevice` | Consultar limites, extensões, formatos e suporte de apresentação. |
| `ID3D12Device` | `VkDevice` | Device lógico; habilitar extensões/features utilizadas. |
| `ID3D12CommandQueue` | `VkQueue` | Filas pertencem a famílias; graphics e present podem diferir. |
| `ID3D12CommandAllocator` | `VkCommandPool` | Reset somente após conclusão dos comandos associados. |
| `ID3D12GraphicsCommandList` | `VkCommandBuffer` | Gravação com `vkCmd*`. |
| `IDXGISwapChain3/4` | `VkSwapchainKHR` + `VkSurfaceKHR` | Superfície associa HWND; swapchain gerencia imagens de apresentação. |
| Backbuffer (`ID3D12Resource`) | `VkImage` da swapchain | Imagens são emprestadas; não destruir com `vkDestroyImage`. |
| Textura/buffer (`ID3D12Resource`) | `VkImage` / `VkBuffer` + `VkDeviceMemory` | Alocação/vinculação explícitas. |
| RTV / DSV | `VkImageView` + attachments de render pass/dynamic rendering | Uso e layouts precisam estar corretos. |
| `ID3D12PipelineState` | `VkPipeline` | Shaders Vulkan em SPIR-V. |
| `ID3D12RootSignature` | `VkPipelineLayout` + `VkDescriptorSetLayout` | Inclui push constants e layouts de descriptors. |
| Shader-visible descriptor heap | `VkDescriptorPool` + `VkDescriptorSet` | Reescrever binding/alocação; não converter handles diretamente. |
| `ResourceBarrier` | `vkCmdPipelineBarrier` | Informar estágios/acessos/layouts; `Barrier2` exige Vulkan 1.3 ou synchronization2 habilitado. |
| Fence da CPU/GPU | `VkFence` (host) / timeline semaphore (GPU) | Timeline requer Vulkan 1.2 ou extensão/feature habilitadas; não usada neste exemplo 1.1. |
| `GetCurrentBackBufferIndex` | `vkAcquireNextImageKHR` | Aquisição sinaliza semaphore/fence; não basta consultar um índice. |
| `ExecuteCommandLists` | `vkQueueSubmit` | Aguardar semáforo de aquisição, sinalizar conclusão do render. |
| `Present` | `vkQueuePresentKHR` | Aguardar conclusão do render; tratar swapchain desatualizada/subótima. |

Ao alternar APIs na mesma janela, aguarde a GPU e destrua a swapchain DXGI e os recursos do backend anterior antes de criar a swapchain Vulkan.

Sequência básica do laço: aguardar o fence do quadro → adquirir imagem → resetar/gravar command buffer → transicionar/usar attachments → submeter aguardando aquisição → apresentar aguardando render concluído. Em `WM_SIZE`, marcar resize; com extensão 0×0, pausar aquisição e recriar swapchain ao restaurar. Mantenha a fila de mensagens Win32 ativa durante operações WSI.

Referências oficiais: [Win32 WSI](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html) e [consulta de apresentação](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceSupportKHR.html).
