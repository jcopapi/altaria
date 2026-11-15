# RELATÓRIO TÉCNICO — DEBUG, CONSOLE E FERRAMENTAS (AGENTE 6)

## 1. ESCOPO DA IMPLEMENTAÇÃO
- **Arquivos criados e modificados**:
  - `game/debug/command_parser.h` (Parser C++17 puro desacoplado com tokenização com aspas, conversões de tipos booleanos/numéricos e distância de Levenshtein).
  - `game/debug/command_registry.h` e `game/debug/command_registry.cpp` (Registro centralizado de comandos, sistema de permissões Safe/Dev/Cheat, categorias, aliases, histórico, despacho de execução).
  - `game/player/flying_player.h` e `game/player/flying_player.cpp` (Implementação de getters/setters e métodos de depuração: `set_noclip`, `is_noclip`, `set_god`, `is_god`, `set_gravity`, `get_gravity`, `reset_debug_parameters`).
  - `game/world_generation/voxel_world.h` e `game/world_generation/voxel_world.cpp` (Adição de métodos de inspeção e manipulação de chunks: `get_tile_info`, `get_loaded_tile_keys`, `reload_tile`, `get_chunk_queue_info`).
  - `native/src/altaria_game.h` e `native/src/altaria_game.cpp` (Integração do console ao `CommandRegistry`, suporte a tecla `F3`, organização da telemetria HUD em seções `[PERFORMANCE]`, `[WORLD]`, `[CHUNKS]`, `[PHYSICS]`).
  - `tests/command_parser_tests.h` e `tests/core_tests.cpp` (Testes unitários automatizados de parsing).
  - `tests/console_runtime.gd` e `tests/run_console_runtime_tests.sh` (Suíte de testes de integração e execução de comandos headless).
  - `tests/benchmarks_runtime.gd` e `tests/run_benchmarks.sh` (Cenários automatizados de benchmark com exportação JSON e CSV).

---

## 2. CARACTERÍSTICAS IMPLEMENTADAS DO SISTEMA DE CONSOLE

### 2.1 Registro Centralizado e Categorias
Cada comando é registrado com metadados estruturados:
- `id`: Identificador principal (ex: `tp`, `chunk info`, `perf`).
- `category`: Categoria de agrupamento (`General`, `Player`, `World`, `Chunks`, `Performance`, `Visual`, `Tools`).
- `description`: Explicação sucinta do propósito.
- `usage`: Sintaxe e parâmetros esperados.
- `permission`: Nível de privilégio mínimo requerido (`Safe`, `Dev`, `Cheat`).
- `aliases`: Nomes alternativos aceitos (ex: `?`, `cls`, `teleport`, `coords`).
- `handler`: Função executora segura com argumentos tokenizados.

### 2.2 Sistema de Permissões
- Três modos de operação configuráveis via comando `permission <safe|dev|cheat>`:
  - **Safe (0)**: Apenas leitura, telemetria, medições e consultas seguras (`help`, `fps`, `perf`, `mem`, `biome info`, `chunk stats`, `pos`, etc.).
  - **Dev (1)**: Controle de iluminação, recarregamento de chunks, shaders e benchmarks (`debug`, `fullbright`, `fog`, `chunk reload`, `spawnpoint set`, `benchmark run`, etc.).
  - **Cheat (2)**: Modificação de estado de jogo e jogador (`noclip`, `god`, `tp`, `speed`, `gravity`, `cut`, `spawn`, etc.).
- Tentativas de executar comandos acima do privilégio atual retornam mensagem informativa segura de bloqueio (`Permission denied`), sem travar o jogo.

### 2.3 Tratamento Seguro de Erros e Sugestões Fuzzy
- Entrada com aspas compostas (ex: `echo "teste de texto"`) é preservada como um único token sem truncamento.
- Quando o usuário digita um comando inexistente, o sistema calcula a distância de Levenshtein contra todos os comandos e aliases registrados, sugerindo os mais próximos (ex: `flyy` -> *"Did you mean: 'fly'?"*; `noclp` -> *"Did you mean: 'noclip'?"*).

### 2.4 Comandos Implementados
- **Gerais**: `help [cmd]`, `clear`, `history`, `echo <texto>`, `version`, `quit`, `permission [safe|dev|cheat]`.
- **Jogador**: `noclip [on|off]`, `fly [on|off]`, `freecam [on|off]`, `tp <x> <y> <z>`, `tp spawn`, `pos`, `speed <val>`, `gravity <val>`, `god [on|off]`, `resetplayer`.
- **Mundo**: `world seed`, `world info`, `world new [seed]`, `seed`, `biome info`, `biome list`, `biome <nome>`, `time get`, `time set <h>`, `time pause`, `time resume`, `weather get`, `weather set <tipo>`, `wind <0..3>`, `temperature [x y z]`, `spawnpoint get`, `spawnpoint set`.
- **Chunks**: `chunk info [x z]`, `chunk list`, `chunk queue`, `chunk stats`, `chunk reload [x z]`.
- **Performance**: `fps`, `perf`, `perf on/off`, `mem`, `render stats`, `physics stats`, `benchmark help`, `benchmark run [preset]`, `benchmark report`.
- **Visual e Ferramentas**: `debug overlay|perf|world|chunks|physics [on|off]`, `fullbright [on|off]`, `fog`, `outline`, `pixelation`, `godrays`, `paint`, `overlay`, `shape`, `cut`, `physics`, `light`, `block`, `spawn`, `cave`.

---

## 3. PAINEL DE DEBUG AVANÇADO (F3 E TELEMETRIA)

- O painel HUD de depuração foi refatorado para operar com **baixo impacto de desempenho**:
  - Taxa de atualização configurável via `hud_interval` (padrão de 0.1s / 10 Hz), evitando formatação custosa de strings a 60/120 frames por segundo.
  - Alternância rápida com a tecla `F3`.
  - Seções modulares toggleáveis individualmente via comando de console:
    - `[PERFORMANCE]`: FPS, frame time instantâneo, triângulos renderizados, tempo de geração de malha.
    - `[WORLD]`: Nome e semente do mundo, coordenadas (X, Y, Z), bioma ativo, temperatura percebida, hora e clima.
    - `[CHUNKS]`: Tiles ativos, distribuição de LODs [0..5], fila de processamento, tempo do primeiro tile e prontidão total.
    - `[PHYSICS]`: Modo de locomoção (FLY/WALK, NOCLIP, GOD), corpos rígidos ativos no Jolt Physics, colisores de terreno.

---

## 4. RESULTADOS DOS TESTES AUTOMATIZADOS E BENCHMARKS

1. **Testes Unitários de Parsing (`tests/run_core_tests.sh`)**:
   - `run_command_parser_tests`: 100% de sucesso em tokenização de aspas, conversão booleana, inteiros, floats e distância Levenshtein.
   - Suíte de 225.109 asserções de integridade do núcleo aprovada sem falhas.

2. **Testes de Integração Headless (`tests/run_console_runtime_tests.sh`)**:
   - `CONSOLE_RUNTIME_PASS`: Cobertura completa de execução de todos os verbos e subcomandos em ambiente Godot headless.
   - Validação de rejeição e concessão de privilégios (`permission safe` bloqueando `noclip`, `permission cheat` desbloqueando).
   - Validação de sugestão de erros de digitação.

3. **Benchmarks Reproduzíveis (`tests/run_benchmarks.sh`)**:
   - Métricas exportadas com sucesso em `raw/benchmark_results.json` e `raw/benchmark_results.csv`:
     - Geração de mundo (Seed 424242): primeiro tile em 137.58 ms; mundo pronto com 460 tiles em 2.31 s; 2.291.470 triângulos.
     - Voo em alta velocidade (80 m/s): frame time médio de 28.12 ms; P95 de 97.96 ms; P99 de 129.38 ms.
     - Teletransporte de estresse (para 5000, 200, 5000): fila máxima de 24 chunks preenchida imediatamente; estabilização completa em 2.75 s.
     - Modificação de chunk e remeshing: tempo de despacho de 102.9 ms; incremento correto de revisão.
     - Telemetria de memória: alocação estática inicial de 65 MB; pico de 76 MB; dados de voxel ocupando 52.290 KB (~51 MB).

---

## 5. RECOMENDAÇÕES PARA PRÓXIMOS PASSOS

- Os arquivos criados (`game/debug/command_parser.h`, `game/debug/command_registry.h`, `game/debug/command_registry.cpp`, testes e benchmarks) são permanentes e totalmente compatíveis com a arquitetura do projeto.
- Recomenda-se integrar a tecla `F1` ou `~` nativa do `CrabConsole` ao autocompletion interativo em tempo real na caixa de texto.
