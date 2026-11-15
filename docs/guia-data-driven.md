# Altaria — guia de extensão data-driven

O Altaria é um jogo voxel em Godot, com geração e gameplay em C++ via GDExtension, FastNoise2 para ruído e Jolt para corpos físicos. Os catálogos JSON descrevem conteúdo e parâmetros. Os sistemas C++ definem os comportamentos que esses catálogos podem selecionar. Os nomes em inglês são o padrão. Use `name_pt` onde o catálogo já oferece tradução.

## Onde mexer

| Quero adicionar ou ajustar | Arquivos e fluxo |
| --- | --- |
| **Bloco** | `game/world_generation/data/blocks.json` → registro validado por `world_data.cpp` → volume, malha e colisão em `voxel_core.cpp`, `terrain_mesh.cpp` e `voxel_world.cpp`. |
| **Textura de bloco** | PNG em `game/assets/blocks/`. Registre o caminho `res://` no array `textures` de `blocks.json`. Aponte `texture_layer` para o índice da camada. Use o tronco como referência para `texture_end_layer`. |
| **Overlay de transição** | `game/world_generation/data/overlays.json`. Defina ID, chave e camadas de textura. A grama usa `transition_overlay` em `blocks.json` para identificar o overlay aplicado na transição com o bloco adjacente. |
| **Bioma de superfície** | `game/world_generation/data/biomes.json` e `biome_stage.cpp`. Copie um bioma próximo em clima, materiais e tintura. Confira onde o gerador distribui o bioma e como `world_atmosphere.cpp` lê suas cores de fog. |
| **Relevo, água de superfície e LOD** | `game/world_generation/data/generation.json`. Frequências e amplitudes, rios, costa, montanhas, vegetação, distâncias e `adaptive_streaming`. Geração e quadtree em `voxel_core.cpp`. Streaming e disponibilidade por altura em `voxel_world.cpp`. |
| **Caverna, câmara ou formação** | `game/world_generation/data/caves.json`, interpretado por `cave_data.cpp`. O array `archetypes` contém ID, limites de profundidade, raios XYZ e peso. `exploration_formations` referencia definições de formação. Planejamento, campo, formações e hidrologia ficam em `game/world_generation/caves/`. |
| **Bioma subterrâneo** | `game/world_generation/data/cave_biomes.json`, `cave_biome_data.cpp` e `caves/cave_biomes.cpp`. Preserve as referências às chaves dos blocos ao definir materiais. |
| **Líquido e reação** | `game/world_generation/data/liquids.json` e `liquid/liquid_system.cpp`. Use o bloco já cadastrado em `block_key`. Ajuste viscosidade, intervalo entre ticks, vazão e reações tomando água e lava como exemplos. Reservatórios procedurais das cavernas passam por `caves/cave_hydrology.cpp`. |
| **Céu, fog, luz e pós-processamento** | `game/world_generation/data/environment.json`, `world_atmosphere.cpp` e `game/world_generation/shaders/`. O contorno e a pixelização ficam em `cartoon_world.gdshader`. Raios solares passam por `addons/lens_effects/`. |
| **Player e entidade** | `game/player/data/entities.json` configura dimensões, movimento e alcance do player e os protótipos de entidades. O controle fica em `game/player/flying_player.cpp`. `native/src/altaria_game.cpp` implementa o spawn. O protótipo `altaria:marker` demonstra o controlador `static_box`. |
| **Forma de construção ou física de blocos** | `game/world_generation/block_shapes.cpp`, `ramp_shapes.cpp`, `construction_core.cpp`, `built_world.cpp` e `physics_blocks.cpp`. Formas como slab e rampa têm geometria e colisão em código. |
| **Comando de debug** | `game/debug/command_registry.cpp` e `command_parser.h`. Reuse o registro para ajuda, parâmetros e permissões. A integração do console passa por `native/src/crab_console_bridge.cpp`. Diagnósticos subterrâneos ficam em `game/debug/cave_debug.cpp`. |
| **Cena principal e integração** | `project.godot`, `scenes/main.tscn`, `native/src/altaria_game.cpp` e `native/src/register_types.cpp`. |

Na tabela, os caminhos abreviados de geração são relativos a `game/world_generation/`.

## Passo a passo

**Bloco decorativo com comportamento existente.** Copie um bloco comum de `blocks.json`, reserve um ID livre e dê uma chave estável como `altaria:basalt_tile`. Defina os nomes, a cor e as flags de sólido e colocável conforme o bloco copiado. Para usar PNG, importe o asset no Godot e referencie sua camada no catálogo. Carregue um mundo com `world new 1337`, selecione com `block altaria:basalt_tile` e teste colocar, quebrar e aplicar `paint`. Para que o bloco apareça naturalmente, ligue a chave ao bioma ou à etapa geradora apropriada.

**Entidade com controlador existente.** Duplique o protótipo `altaria:marker` no array `entities`, troque chave, nome, tamanho e cor e valide com `spawn altaria:<chave>`. O spawn genérico atual aceita `static_box`. IA, animação ou outro controlador exigem implementação C++ e integração ao spawn.

**Câmara procedural.** Copie um item de `caves.json` → `archetypes`, reserve um ID e ajuste `min_depth`, `max_depth`, `radius_min`, `radius_max` e `weight`. Use seeds fixas para comparar distribuição, entradas e conexões. Formas procedurais ou regras de conexão novas pertencem ao planejador de cavernas.

## Como validar

1. Preserve IDs e chaves existentes. Para conteúdo novo, copie um item do mesmo tipo e altere o mínimo necessário.
2. Use `world new 1337` para recarregar a geração em uma seed reproduzível. Para alterações de entidades ou recursos importados, reinicie o jogo se precisar recarregar a definição ou o asset.
3. Para mudanças C++, rode `./build.sh platform=linux target=template_debug`. O setup está no `README.md` da raiz.
4. Rode `bash tests/run_core_tests.sh`. Para cavernas, use os scripts `tests/run_cave_*.sh` correspondentes ao subsistema. Para streaming e colisão, use `tests/dynamic_streaming_runtime.gd` seguindo o manifesto do benchmark. Visual e GPU devem ser verificados em Vulkan.
5. Use uma sessão descartável. O fluxo de teste gera um mundo novo. As edições feitas naquela sessão sobrevivem ao descarregamento de chunks.

## Contexto para o próximo chat

- A geração ativa usa o perfil 3, com cavernas, LODs voxel e streaming adaptativo por deslocamento, fog e profundidade.
- Mantenha as entradas de cavernas visíveis da superfície. Preserve as verificações XYZ de disponibilidade em seleção, física e líquidos ao mudar o streaming.
- Iluminação estilizada: paleta de sol, céu, preenchimento de sombra (`sky_fill_*`), grade split-tone (`grade_*`) e cores das nuvens ficam em `environment.json`, aplicadas em mundos existentes. A camada de nuvens usa `cloud_base_y`/`cloud_thickness` (Y absoluto). Com o relevo atual só cerca de 1% do terreno passa do núcleo da camada.
- Superfície: a geração usa só cubos e slabs. Com `surface_shapes` em `generation.json`, degraus de um bloco ganham uma slab na frente (meio degrau). Morros em forma de sino saem de `hill_*` em `generation.json` (`mound_height` em `voxel_core.cpp`).
- Árvores: galhos com cotovelos, T e tocos (`branch_elbow_up/down`, `branch_tee_up`, `branch_stub`) e blobs de folhas por ponta, em `vegetation.cpp`.
- Água (`water.gdshader`): faixas de profundidade aqua→violeta, cáusticas, espuma pixelada na margem e brilhos. Lava tem material próprio (`lava.gdshader`, roteado em `VoxelWorld::add_liquid_surfaces`) e luzes quentes por poça (`lava_light_anchors` em `terrain_mesh.cpp`).
- Lava subterrânea aparece a partir de `underground_lava_min_depth` (2) em `caves.json`; câmaras mais fundas têm prioridade.
- Nuvens são blocos voxel (DDA em `world_clouds.gdshaderinc`); `cloud_pixel_size` × 500 = tamanho do bloco e `cloud_above_opacity` controla a transparência vista de cima.
- Árvores formam bosques por manchas de ruído (`forest_patch` em `vegetation.cpp`); fora deles só aparecem árvores isoladas.
- O sistema experimental Terrain foi removido a pedido. O jogo tem formas de construção como slabs e rampas.
- O benchmark atual encontrou ganho de P99 e um pico isolado de voo perto de 2,3 s do percurso. Veja [relatório de streaming](performance/caca-as-bruxas/2026-11-11-dynamic-streaming/report.md).
- Referências adicionais em [geração de mundo](../game/world_generation/README.md), [player](../game/player/README.md), `docs/caves/` e `docs/performance/`.
