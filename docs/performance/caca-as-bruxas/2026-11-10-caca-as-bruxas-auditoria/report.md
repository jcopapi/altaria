# OPERAÇÃO CAÇA ÀS BRUXAS — RELATÓRIO CONSOLIDADO EXECUTIVO

**Projeto**: Altaria (Sandbox RPG Voxel 3D em Godot 4.7.1 GDExtension C++)  
**Operação**: `2026-11-10-caca-as-bruxas-auditoria`  
**Ambiente de Auditoria**: 13th Gen Intel Core i7-13620H (16 vCPUs), 16 GB RAM, NVIDIA GeForce RTX 4050 Mobile, Linux x86_64  
**Motor & Dependências**: Godot 4.7.1.stable.mono.gdstudio.a450b93bf, FastNoise2 1.1.1, Jolt Physics 3D  
**Princípio da Operação**: *Investigar, Medir, Pesquisar, Experimentar, Reverter e Documentar.*

---

## 1. RESUMO EXECUTIVO

### 1.1 Estado Geral do Projeto
O *Altaria* apresenta uma arquitetura técnica robusta e moderna baseada em Godot 4.7 com GDExtension em C++17. A separação entre o motor principal e as bibliotecas especializadas (FastNoise2 para geração de terreno procedural e Jolt Physics para dinâmica de corpos rígidos) oferece uma base de altíssimo desempenho quando comparada a implementações em GDScript puro.

### 1.2 Principais Forças da Arquitetura
1. **Pipeline de Multithreading Assíncrono (`TerrainWorkers`)**: A geração volumétrica e o cálculo de malhas ocorrem em threads de fundo desacopladas da thread principal, permitindo streaming contínuo sem travamento total do jogo.
2. **Sistema Híbrido de Formas e Rampas Cartoon**: Suporte volumétrico e de colisão analítica exata a 8 tipos geométricos distintos (cubo, slab, slab vertical, rampas, escadas, tetos, pilares e mini-pilares) com iluminação celular estilizada.
3. **Colisão Cinemática Analítica (`collides_box`)**: A locomoção do jogador humano é matematicamente resolvida contra a grade de voxels, imune a "tunelamento" e sem dependência síncrona do motor de física para movimentação básica.

### 1.3 Principais Fraquezas e Riscos Identificados
1. **Avaliações Analíticas Redundantes de Ruído**: Múltiplos nós FastNoise2 2D/3D (continental, montanha, erosão, warp, bioma) são reavaliados repetidamente para as mesmas coordenadas durante a semeadura de árvores (`vegetation.cpp`) e correntes de rios (`liquid_system.cpp`).
2. **Gargalo de Upload na Thread Principal (`VoxelWorld::upload`)**: Cada chunk LOD0 pronto transfere vértices alocando 8 `PackedArrays` individuais e constrói um `ConcavePolygonShape3D` Jolt na thread principal, gerando picos de frame time (P99 de 129.38 ms em voo rápido).
3. **Descarte Lento de Chunks Aposentados (`reconcile`)**: Chunks fora do raio de visão são drenados a uma taxa rígida de no máximo 2 tiles por frame, causando represamento transitório de até ~54 MB de dados volumétricos após teletransportes rápidos.
4. **Desacoplamento entre Biomas 3D e Clima 2D (Problema dos Snowfields a 23 °C)**: O terreno reclassifica topos de montanha como "Snowfields" via resfriamento de ruído unitário, mas o sistema de atmosfera consulta a macro-célula Voronoi 2D de planície (Meadow a 24 °C) com taxa de lapso física terrestre modesta (0.0065 °C/m), exibindo neve sob temperatura aparente tropical.

### 1.4 Estimativa do Potencial de Ganho Global
- **Geração de Chunks**: Redução de **30% a 45%** no tempo médio de geração de malhas e preenchimento de terreno.
- **Estabilidade de Frame Time**: Redução de mais de **60%** nos picos de frame time (P99 caindo de 129 ms para menos de 50 ms).
- **Geometria em Cena**: Eliminação imediata de **98.304 triângulos** completamente ocultos na base de bedrock de cada mundo carregado.

---

## 2. RANKING GERAL DE PROBLEMAS

| Prioridade | ID | Área | Descrição Resumida | Gravidade | Facilidade de Correção | Ganho Potencial |
| :---: | :---: | :---: | :--- | :---: | :---: | :---: |
| **1** | **BIOM-01** | Biomas/Clima | Snowfields com temperatura tropical (23 °C) por desacoplamento 3D/2D e taxa de lapso incompatível. | Alta | Alta | Coerência ecológica e visual absoluta. |
| **2** | **PROC-01** | Procedural | Loop de árvores reavalia 9 nós FastNoise2 2D por candidato em vez de ler `volume.columns`. | Alta | Alta | -72% no tempo de vegetação (8-14 ms por chunk denso). |
| **3** | **PERF-02** | Performance | 8 alocações dinâmicas de `PackedArray` na thread principal por upload de chunk. | Alta | Média | Eliminação de engasgos durante movimentação rápida. |
| **4** | **MEM-01** | Memória | Dreno de chunks aposentados limitado a 2/frame, represando 50+ MB após teletransporte. | Alta | Alta | Liberação imediata de RAM após teletransporte/viagem. |
| **5** | **FIS-01** | Física | Construção síncrona de BVH de `ConcavePolygonShape3D` na thread principal (picos de 7ms). | Alta | Média | Redução direta de picos de frame time. |
| **6** | **PROC-02** | Procedural | `LiquidSimulation::current_at` chama `column()` e depois `river_current()` que re-executa `column()`. | Média | Alta | Alívio contínuo de CPU no loop de líquidos. |
| **7** | **PERF-01** | Performance | Geração de quads inferiores de bedrock voltados para baixo em `min_y = -64`. | Média | Alta | -98.304 triângulos inúteis eliminados por cena. |
| **8** | **FIS-02** | Física | 49 colisores Jolt trimesh gerados mesmo sem nenhum corpo físico dinâmico ativo. | Média | Média | Economia de memória e processamento de física. |
| **9** | **MEM-02** | Memória | `Volume` denso aloca 892 KB por chunk mesmo em áreas planas uniformes. | Média | Baixa | Potencial de -80% de memória de terreno com RLE. |
| **10** | **PERF-03** | Performance | Raycast de seleção executado incondicionalmente a cada frame sem movimento de câmera. | Baixa | Alta | -0.2ms a cada tick de renderização. |
| **11** | **MEM-03** | Memória | `CaveCache` acumula regiões de cavernas sem teto máximo LRU. | Baixa | Alta | Prevenção de vazamento lento em sessões de várias horas. |
| **12** | **PROC-03** | Procedural | Recálculo redundante de arcos naturais em passes repetidos de meshing. | Baixa | Média | Otimização incremental de meshing worker. |

---

## 3. OTIMIZAÇÕES COMPROVADAS (VALIDADAS POR EXPERIMENTOS)

### 3.1 EXP-PROC-01 — Reúso de Colunas na Geração de Vegetação
- **O que foi testado**: Modificação do loop de candidatos a árvores em `vegetation.cpp` para consultar a grade contígua de colunas já calculadas pelo `Volume` (`volume.columns`), eliminando reavaliações analíticas de `FastNoise2`.
- **Métricas Antes**: Tempo médio de `build_vegetation`: **11.42 ms** por chunk (bioma Jungle).
- **Métricas Após**: Tempo médio de `build_vegetation`: **3.18 ms** por chunk (**-72.1% de redução de tempo**).
- **Por que funciona**: Elimina entre 40 e 96 avaliações de nós de ruído trigonométrico e fractal por chunk.

### 3.2 EXP-PERF-01 — Supressão da Face Inferior de Bedrock
- **O que foi testado**: Omissão dos quads virados para baixo (`plane == -1 && d == 1`) na camada `min_y = -64` em `terrain_mesh.cpp`.
- **Métricas Antes**: **2.291.470 triângulos** totais no cenário de benchmark inicial.
- **Métricas Após**: **2.193.166 triângulos** totais (**-98.304 triângulos eliminados**).
- **Por que funciona**: Nenhuma câmera de jogo convencional consegue visualizar o mundo a partir de uma coordenada inferior a -64 olhando para cima; toda a geometria eliminada era 100% ocluída por definição.

### 3.3 EXP-MEM-01 — Esvaziamento Acelerado pós-Teletransporte
- **O que foi testado**: Remoção do limitador de 2 tiles/frame quando há detecção de teletransporte (viewer deslocado além da distância de visão).
- **Métricas Antes**: Pico de memória estática de **76 MB**; 49 tiles obsoletos retidos ao longo de 25 frames (~416 ms).
- **Métricas Após**: Pico de memória estática estabilizado em **68 MB** (**-8 MB imediatos**); 0 tiles obsoletos retidos a partir do 2º frame pós-teletransporte.
- **Por que funciona**: A glibc e o allocator do Godot recebem a instrução de desalocação em bloco imediatamente após a quebra de localidade espacial.

### 3.4 EXP-FIS-01 — Instanciação Preguiçosa de Colisores Jolt
- **O que foi testado**: Supressão da criação síncrona de `ConcavePolygonShape3D` para chunks que não possuem corpos rígidos de `PhysicsBlocks` num raio de 48 metros.
- **Métricas Antes**: Pico de frame time P99: **129.38 ms**; tempo de upload com física: **14.8 ms**.
- **Métricas Após**: Pico de frame time P99: **48.2 ms** (**-62.7% de redução no pior caso**); tempo de upload: **6.1 ms**.
- **Por que funciona**: Como `FlyingPlayer` utiliza colisão analítica própria (`collides_box`), a ausência do colisor trimesh não afeta o jogador humano, aliviando a thread principal.

### 3.5 EXP-BIOM-01 — Sincronização Térmica Coluna -> Atmosfera
- **O que foi testado**: Fazer `get_temperature()` consultar a coluna do terreno local em vez da macro-célula Voronoi 2D desacoplada, aplicando taxa de resfriamento coerente com mundos de escala comprimida.
- **Métricas Antes**: Coordenada de pico nevado exibindo: `Biome: Snowfields` e `Temperature: +22.8 °C`.
- **Métricas Após**: Coordenada de pico nevado exibindo: `Biome: Snowfields` e `Temperature: -4.6 °C`.
- **Por que funciona**: Alinha a fonte de dados do HUD com o mesmo algoritmo que decide a geração dos blocos de neve.

---

## 4. OTIMIZAÇÕES PROMISSORAS (PARA IMPLEMENTAÇÃO PROFUNDA)

1. **FastNoise2 Vectorized Grid Generation (`GenUniformGrid3D`)**:
   - **Descrição**: Substituir os loops escalares aninhados `density->GenSingle3D(wx, wy, wz)` por blocos contíguos alinhados a 32 bytes via `GenUniformGrid3D`.
   - **Ganhos Esperados**: Redução de 45% a 60% no tempo da etapa `terrain_stage_ms`.
   - **Pré-requisitos**: Adequar os strides de memória da matriz 3D do `Volume` para corresponder à ordem X-Y-Z exigida pela biblioteca.
   - **Riscos**: Baixo, mantendo testes de regressão contra as seeds conhecidas.

2. **Greedy Meshing Vertical Aprimorado**:
   - **Descrição**: Agrupar quads coplanares não apenas na horizontal, mas também verticalmente para materiais de rocha contígua em penhascos e paredes de cavernas.
   - **Ganhos Esperados**: Redução de 15% a 25% na contagem de triângulos em áreas subterrâneas e montanhas.
   - **Pré-requisitos**: Mapeamento de UV com suporte a escala não-uniforme nos shaders cartoon.

3. **Compressão RLE para Chunks Inativos**:
   - **Descrição**: Descartar a matriz de 892 KB de blocos para chunks puramente estáticos que já geraram malha e colisor, recriando-a sob demanda somente se o jogador quebrar ou colocar blocos no chunk.
   - **Ganhos Esperados**: Redução de mais de 75% no consumo total de RAM de terreno (de ~54 MB para < 14 MB).
   - **Pré-requisitos**: Pipeline de descompressão instantânea baseado na semente e histórico de edições.

---

## 5. HIPÓTESES DESCARTADAS OU INCONCLUSIVAS

1. **Hipótese de Vazamento de Memória nos Buffers do Godot (`ArrayMesh` / `RenderingServer`)**:
   - *Investigação*: Suspeitava-se de vazamento permanente de VRAM/RAM em `VoxelWorld::upload`.
   - *Resultado*: **Descartada**. Os objetos `ArrayMesh` e nós `MeshInstance3D` são desalocados perfeitamente pelo mecanismo `Ref<T>` e `memdelete` do Godot. O acúmulo de memória observado decorria estritamente do dreno limitado a 2 tiles/frame em `reconcile()`.
2. **Hipótese de Concorrência e Corrupção de Estado no FastNoise2**:
   - *Investigação*: Suspeitava-se que chamadas simultâneas de múltiplos workers de terreno pudessem colidir internamente no gerador de ruído.
   - *Resultado*: **Descartada**. As árvores de nós do FastNoise2 são estritamente imutáveis após a configuração inicial (`Settings`), operando com total isolamento thread-safe.

---

## 6. PESQUISA EXTERNA E TECNOLOGIAS RECOMENDADAS

1. **FastNoise2 (Licença MIT)**:
   - Repositório: https://github.com/Auburn/FastNoise2
   - Versão no projeto: 1.1.1 estática.
   - Recomendação: Explorar as APIs de amostragem em bloco (`GenUniformGrid2D` e `GenUniformGrid3D`), que utilizam instruções vetoriais nativas AVX2 no processador do usuário (Intel Core i7-13620H).
2. **Jolt Physics (Licença MIT)**:
   - Repositório: https://github.com/jrouwe/JoltPhysics
   - Integração: `godot-jolt` GDExtension.
   - Recomendação: Avaliar `HeightFieldShape3D` para superfícies sem saliências e construção de BVH assíncrona fora da thread de frame.
3. **Padrão de Memory Pooling para Chunks**:
   - Alocar um pool pré-aquecido de buffers `Volume` (ex: 64 volumes reutilizáveis), trocando `new`/`delete` contínuos por reinicialização de ponteiro via `std::vector::clear()`.

---

## 7. FERRAMENTAS IMPLEMENTADAS NA OPERAÇÃO

Como parte dos objetivos da Operação Caça às Bruxas, foram desenvolvidas e integradas ferramentas permanentes ao repositório:

1. **Sistema Centralizado de Console (`altaria::CommandRegistry` & `altaria::CommandParser`)**:
   - Implementado em C++17 nativo desacoplado (`game/debug/command_parser.h`, `game/debug/command_registry.h`, `game/debug/command_registry.cpp`).
   - 7 categorias de comandos, 3 níveis de privilégio (Safe, Dev, Cheat), suporte a aspas, histórico, aliases e sugestão inteligente de comandos via Levenshtein.
   - Cobertura completa de todos os comandos requeridos: `help`, `echo`, `version`, `quit`, `permission`, `noclip`, `fly`, `freecam`, `tp`, `pos`, `speed`, `gravity`, `god`, `resetplayer`, `world`, `biome`, `time`, `weather`, `wind`, `temperature`, `chunk`, `fps`, `perf`, `mem`, `render stats`, `physics stats`, `benchmark`, `debug`, `fullbright`, `fog`, etc.
2. **Painel de Debug Avançado Modular (F3 & HUD)**:
   - HUD estruturado em blocos toggleáveis individualmente: `[PERFORMANCE]`, `[WORLD]`, `[CHUNKS]`, `[PHYSICS]`.
   - Taxa de atualização configurável (10 Hz), eliminando o impacto no frame rate de medições.
3. **Infraestrutura de Testes Automatizados**:
   - `tests/command_parser_tests.h`: Testes unitários puros de parsing, integrados ao executável `core_tests`.
   - `tests/console_runtime.gd` & `tests/run_console_runtime_tests.sh`: Suíte de integração e validação de comandos em execução Godot headless.
4. **Infraestrutura de Benchmarks Reproduzíveis**:
   - `tests/benchmarks_runtime.gd` & `tests/run_benchmarks.sh`: Medição automatizada de geração, voo rápido, teletransporte, modificação de chunk e memória, exportando resultados em:
     - `raw/benchmark_results.json`
     - `raw/benchmark_results.csv`

---

## 8. ROTEIRO SUGERIDO PARA O IMPLEMENTADOR (CODEX ROADMAP)

Para a equipe responsável pela implementação definitiva das melhorias apontadas nesta auditoria, recomenda-se a seguinte ordem de execução:

### Fase 1 — Consistência de Dados e Correções Imediatas (Sem Risco de Quebra)
- **Tarefa 1.1**: Corrigir `BIOM-01` — Sincronizar o bioma e temperatura de `AltariaGame::update_hud` com a coluna do terreno, e calibrar `lapse_c_per_m` em `biomes.json` para escala de montanha de jogo.
- **Tarefa 1.2**: Corrigir `PROC-02` — Sobrecarga de `river_current` em `liquid_system.cpp` reutilizando a `Column` já amostrada.
- **Tarefa 1.3**: Corrigir `PERF-01` — Suprimir a geração da malha inferior de bedrock em `terrain_mesh.cpp:330`.
- **Critério de Aceitação**: 225.109 testes do núcleo continuam passando (`./tests/run_core_tests.sh`); montanhas nevadas mostram temperatura coerente no HUD; redução de 98.304 triângulos confirmada no benchmark.

### Fase 2 — Otimizações de Streaming e Memória
- **Tarefa 2.1**: Implementar `PROC-01` — Passar as colunas já computadas para `Generator::trees` em `vegetation.cpp`.
- **Tarefa 2.2**: Implementar `MEM-01` — Dreno acelerado de chunks aposentados em saltos espaciais em `voxel_world.cpp:473`.
- **Tarefa 2.3**: Implementar `PERF-03` — Guarda condicional de raycast em `AltariaGame::_process`.
- **Critério de Aceitação**: Queda no tempo de vegetação em biomas densos validada via benchmark; retenção de RAM pós-teletransporte estabilizada em menos de 70 MB.

### Fase 3 — Otimizações Arquiteturais de Physics e Renderização
- **Tarefa 3.1**: Implementar `FIS-01` / `FIS-02` — Instanciação preguiçosa de colisores trimesh Jolt apenas no raio de corpos rígidos dinâmicos.
- **Tarefa 3.2**: Implementar `PERF-02` — Estruturação de buffers estáticos para diminuir alocações na thread principal durante uploads.
- **Critério de Aceitação**: P99 de frame time em voo rápido abaixo de 50 ms no benchmark automatizado (`./tests/run_benchmarks.sh`).

---
*Relatório concluído e certificado pela Coordenação da Operação Caça às Bruxas.*
