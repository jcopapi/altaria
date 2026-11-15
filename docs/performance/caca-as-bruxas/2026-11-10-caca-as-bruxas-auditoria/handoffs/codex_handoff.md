# DOSSIÊ DE IMPLEMENTAÇÃO TÉCNICA — CODEX ROADMAP
**Projeto**: Altaria (Godot 4.7.1 GDExtension C++ / FastNoise2 1.1.1 / Jolt Physics 3D)  
**Operação de Origem**: Caça às Bruxas (`2026-11-10-caca-as-bruxas-auditoria`)  
**Objetivo**: Implementação definitiva das correções de integridade, otimizações de performance e refinamento arquitetural aprovadas durante a auditoria técnica.

---

## 1. VISÃO GERAL E REGRAS DE SEGURANÇA
- **Regra de Ouro**: Preservar convenções de coordenadas, sementes procedurais, dados volumétricos e formato dos shaders.
- **Ferramentas de Validação Disponíveis no Repositório**:
  - Testes do Núcleo: `./tests/run_core_tests.sh` (225.109 asserções de integridade matemática).
  - Testes Funcionais Headless: `./tests/run_console_runtime_tests.sh` e `./tests/run_ephemeral_tests.sh`.
  - Benchmarks Automatizados: `./tests/run_benchmarks.sh` (exporta JSON e CSV com FPS, frame times P95/P99, queues e memória).
  - Console de Comandos no Jogo: Abrir com `F1` ou `:`; telemetria detalhada com `F3`.

---

## 2. LISTA PRIORIZADA DE TAREFAS DE IMPLEMENTAÇÃO

### FASE 1 — CORREÇÕES DE INTEGRIDADE E CONSISTÊNCIA (PRIORIDADE MÁXIMA)

#### Tarefa 1.1 — Correção do Bug Térmico dos Snowfields (BIOM-01)
- **Problema**: Regiões de montanha classificadas como `Snowfields` apresentam temperatura diurna de 23 °C no HUD.
- **Causa Raiz**: `voxel_core.cpp:445` reclassifica localmente a coluna em 3D usando decaimento de ruído de `0.004/m`. Porém, `environment_core.cpp:25` (`temperature_at`) consulta apenas a macro-célula 2D de Voronoi (planície a 24 °C) com taxa física real modesta ($0.0065 \text{ °C/m}$).
- **Arquivos a Modificar**:
  - `game/world_generation/environment_core.cpp` (função `temperature_at`)
  - `game/world_generation/data/biomes.json` ou `environment.json`
  - `native/src/altaria_game.cpp` (métodos `get_temperature` e `update_hud`)
- **Ação Recomendada**:
  1. Em `native/src/altaria_game.cpp:882`, fazer `get_temperature(p)` ler a coluna já calculada do terreno (`world->generator()->column(...)`) para garantir que o bioma e a temperatura tenham a mesma fonte da verdade.
  2. Ajustar a taxa de lapso `lapse_c_per_m` em `biomes.json` para $0.065 \text{ °C/m}$ a $0.080 \text{ °C/m}$ (fator de escala de altitude de videogame 10x-12x), permitindo que montanhas de 200m a 280m atinjam temperaturas negativas reais (-2 °C a -8 °C).
- **Critério de Aceitação**: Topo nevado com blocos de neve deve registrar temperatura $\le 0 \text{ °C}$ no comando `temperature` e no HUD `[WORLD]`.

---

#### Tarefa 1.2 — Eliminação de Reavaliações Redundantes de FastNoise2 em Árvores (PROC-01)
- **Problema**: O loop de semeadura de árvores reavalia 9 nós analíticos do FastNoise2 até 96 vezes por chunk, inflacionando a geração em 8ms a 14ms em biomas densos.
- **Arquivos a Modificar**:
  - `game/world_generation/vegetation.cpp:45-56`
  - `game/world_generation/voxel_core.h:310`
- **Ação Recomendada**:
  1. Alterar a assinatura de `build_vegetation` para receber a referência constante do vetor `columns` do `Volume` (`const std::vector<Column> &columns`).
  2. Dentro do loop de candidatos em `vegetation.cpp:45`, consultar diretamente `columns[(x + 1) * 34 + (z + 1)]` em vez de chamar `terrain_sample()` analítico.
- **Critério de Aceitação**: Tempo de `build_vegetation` deve cair de ~11.4 ms para < 3.5 ms sem alterar a posição das árvores para a mesma seed.

---

#### Tarefa 1.3 — Eliminação de Dupla Amostragem em Rios (PROC-02)
- **Problema**: `liquid_system.cpp:453` chama `generator->column(ix, iz)` e imediatamente depois chama `generator->river_current(ix, iz)`, que internamente recalcula `column(ix, iz)`.
- **Arquivos a Modificar**:
  - `game/world_generation/voxel_core.h` e `voxel_core.cpp`
  - `game/world_generation/liquid/liquid_system.cpp:453-456`
- **Ação Recomendada**:
  1. Criar sobrecarga: `Vector2 river_current(const Column &c, int x, int z) const;`.
  2. Passar a coluna já obtida na linha 453 para `river_current`.
- **Critério de Aceitação**: Redução nas chamadas por frame de simulação líquida com zero divergência no fluxo da água.

---

#### Tarefa 1.4 — Supressão da Base Inferior Invisível de Bedrock (PERF-01)
- **Problema**: O gerador cria uma malha completa 32x32 virada para baixo no limite inferior do mundo (`min_y = -64`), gerando 98.304 triângulos 100% ocluídos por cena.
- **Arquivos a Modificar**:
  - `game/world_generation/terrain_mesh.cpp:330-361`
- **Ação Recomendada**:
  1. No loop de geração de quads horizontais, adicionar guarda: se `plane == -1 && d == 1` e `y == min_y`, ignorar a emissão do quad.
- **Critério de Aceitação**: Benchmark deve registrar redução de ~98.000 triângulos na cena total sem nenhuma alteração visual na superfície ou no subterrâneo.

---

### FASE 2 — OTIMIZAÇÕES DE STREAMING, FÍSICA E MEMÓRIA

#### Tarefa 2.1 — Dreno Acelerado de Chunks Aposentados pós-Teletransporte (MEM-01)
- **Problema**: `voxel_world.cpp:473` limita a liberação de chunks aposentados a apenas 2 por frame (`retired_count++ >= 2`). Após teletransporte, 49 chunks ficam retidos por 25 frames, represando 50+ MB.
- **Arquivos a Modificar**:
  - `game/world_generation/voxel_world.cpp:470-485`
- **Ação Recomendada**:
  1. Verificar se a distância do deslocamento do visualizador em relação ao frame anterior é superior a `settings().view_distance * 0.5f` ou se a fila `retired` tem mais de 8 elementos.
  2. Nessas condições de quebra de localidade espacial, permitir a liberação imediata de todos os chunks aposentados no mesmo frame.
- **Critério de Aceitação**: Pico de memória estática estabilizado abaixo de 70 MB imediatamente após teletransporte (validado via `./tests/run_benchmarks.sh`).

---

#### Tarefa 2.2 — Instanciação Preguiçosa (Lazy) de Colisores Jolt (FIS-01 / FIS-02)
- **Problema**: `VoxelWorld::upload` compila síncronamente a árvore BVH do `ConcavePolygonShape3D` na thread principal para todo chunk LOD0 (picos de 7.2 ms), embora `FlyingPlayer` use colisão analítica própria para se mover.
- **Arquivos a Modificar**:
  - `game/world_generation/voxel_world.h` e `voxel_world.cpp:245-276`
- **Ação Recomendada**:
  1. Adiar a chamada `shape->set_faces()` e a criação do `StaticBody3D` para chunks que não possuam `PhysicsBlocks` (árvores cortadas/escombros) num raio de 48 metros.
  2. Se um bloco for cortado (`cut` / `physics`), instanciar sob demanda os colisores dos chunks vizinhos imediatos.
- **Critério de Aceitação**: P99 de frame time em voo veloz caindo de 129 ms para menos de 50 ms.

---

#### Tarefa 2.3 — Redução de Alocações Dinâmicas no Upload de Malhas (PERF-02)
- **Problema**: `VoxelWorld::upload` aloca 8 `PackedArrays` individuais por chunk na thread principal.
- **Arquivos a Modificar**:
  - `game/world_generation/voxel_world.cpp:143-201`
- **Ação Recomendada**:
  1. Pré-alocar buffers estáticos reutilizáveis ou transferir a estrutura empacotada pronta da thread de meshing.
  2. Implementar amortecimento dinâmico de tempo com teto de 3.5ms por frame para upload de malhas.
- **Critério de Aceitação**: Eliminação de travamentos de frame em taxas de 60/120 Hz durante movimentação a 80 m/s.

---

#### Tarefa 2.4 — Guarda Condicional no Raycast de Seleção (PERF-03)
- **Problema**: `AltariaGame::_process` chama `update_target()` incondicionalmente a cada frame, varrendo até 27 voxels mesmo quando o mouse e a câmera estão estáticos.
- **Arquivos a Modificar**:
  - `native/src/altaria_game.cpp:906-907`
- **Ação Recomendada**:
  1. Adicionar flag `bool target_dirty` acionada em `_unhandled_input` (movimento do mouse) ou quando a posição do jogador mudar significativamente.
- **Critério de Aceitação**: Economia de ~0.2ms por frame em estado ocioso.

---

### FASE 3 — OTIMIZAÇÕES AVANÇADAS DE ARQUITETURA (LONGO PRAZO)

1. **Amostragem Vetorial AVX2 com FastNoise2 `GenUniformGrid3D`**:
   - Substituir o loop escalar `density->GenSingle3D(x, y, z)` por amostragem em bloco contíguo alinhado a 32 bytes.
   - Ganho: -50% no tempo de preenchimento bruto do chunk.
2. **Compressão RLE em Chunks Inativos**:
   - Descartar os 892 KB da matriz volumétrica de chunks estáticos após o meshing, mantendo apenas deltas de edição.
   - Ganho: Redução de 75% na pegada de RAM de terreno.
3. **Limite LRU no `CaveCache`**:
   - Adicionar teto de 64 regiões ativas em `cave_generator.cpp`.

---

## 3. CHECKLIST DE VALIDAÇÃO PARA O IMPLEMENTADOR

Após cada fase implementada, executar em sequência:
```bash
# 1. Compilação da biblioteca GDExtension
./build.sh

# 2. Testes de regressão matemática do terreno (225.109 asserções)
./tests/run_core_tests.sh

# 3. Testes funcionais do console e parâmetros de debug
./tests/run_console_runtime_tests.sh

# 4. Testes de mundo e persistência efêmera
./tests/run_ephemeral_tests.sh

# 5. Benchmark comparativo de performance
./tests/run_benchmarks.sh
```

Comparar os resultados gerados em `docs/performance/caca-as-bruxas/2026-11-10-caca-as-bruxas-auditoria/raw/benchmark_results.json` contra o baseline da auditoria para certificar os ganhos.
