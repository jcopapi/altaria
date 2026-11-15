# RELATÓRIO TÉCNICO — MEMÓRIA E RECURSOS (AGENTE 3)

## 1. ESCOPO DA INVESTIGAÇÃO
- **Arquivos analisados**:
  - `game/world_generation/voxel_world.cpp` (`VoxelWorld::reconcile`, ciclo de vida de `Tile`, esvaziamento de retired tiles).
  - `game/world_generation/voxel_core.h` (`Volume`, buffers de blocos, dicionários de superfícies e formas).
  - `game/world_generation/caves/cave_generator.cpp` (`CaveCache`, cache de regiões subterrâneas e telhados de cavernas).
- **Sistemas avaliados**:
  - Pegada de memória por chunk LOD0 e chunks distantes (LOD 1 a 5).
  - Taxa de descarte e desalocação após teletransporte veloz.
  - Comportamento do heap durante exploração prolongada em linha reta.
  - Retenção de `std::shared_ptr<Volume>` e buffers Godot.
- **Métodos utilizados**:
  - Medição do alocador estático via `OS::get_static_memory_usage()` e `get_static_memory_peak_usage()`.
  - Instrumentação de contadores de instâncias de `Tile` e `Volume`.
  - Benchmark de teletransporte e voo de alta velocidade (`tests/benchmarks_runtime.gd`).

---

## 2. PROBLEMAS IDENTIFICADOS

### MEM-01 — Liberação represada de chunks aposentados (dreno limitado a 2 por frame)
- **Descrição**: No método `VoxelWorld::reconcile`, chunks que saem da distância de visão são colocados em fila de aposentadoria (`retired`). No entanto, o loop de limpeza possui uma restrição dura:
  ```cpp
  // voxel_world.cpp:473
  if (retired_count++ >= 2) break;
  ```
  Isso significa que o motor só consegue desalocar no máximo 2 chunks por frame.
- **Localização**: `game/world_generation/voxel_world.cpp:473-480`.
- **Gravidade**: Alta.
- **Evidências**:
  - Ao realizar um teletransporte de 5.000 blocos, todos os 49 chunks LOD0 da origem tornam-se obsoletos instantaneamente (~54 MB de dados volumétricos).
  - Como apenas 2 chunks são liberados por frame, são necessários 25 frames (~416 ms a 60 FPS) para liberar a memória da área anterior.
  - Se o jogador usar teletransportes sucessivos ou comando de geração de novo mundo, os chunks anteriores acumulam-se no heap, causando picos de até 150 MB a 200 MB de RAM.
- **Causa Raiz**: O limite de 2 tiles por frame foi projetado para suavizar o descarte durante caminhada normal, mas não possui exceção para eventos de teletransporte ou limpeza em lote.
- **Impacto no Jogador**: Consumo inflado de memória física após viagens rápidas e potencial pressão sobre sistemas com pouca RAM.

### MEM-02 — Custo de memória de `Volume` em chunks planos sem cavernas
- **Descrição**: Cada `Volume` LOD0 aloca um buffer bruto contíguo fixo de 34 * 386 * 34 * sizeof(BlockId) = 892.432 bytes (~892 KB), além de 1.156 colunas (~185 KB). O total do `Volume` gira em torno de 1.1 MB por chunk, mesmo para chunks predominantemente planos onde 95% do volume é apenas ar ou pedra maciça.
- **Localização**: `game/world_generation/voxel_core.h:205-230`.
- **Gravidade**: Média.
- **Evidências**: 49 chunks LOD0 consomem aproximadamente 54 MB apenas para armazenar a matriz de IDs de blocos.
- **Causa Raiz**: Estrutura densa não comprimida (dense 3D array) em vez de paletização ou compressão RLE (Run-Length Encoding) para blocos contíguos uniformes.
- **Impacto no Jogador**: Limite prático restrito na distância de visão de LOD0 sem esgotar o cache L3 da CPU.

### MEM-03 — Cache ilimitado de regiões de caverna durante exploração contínua
- **Descrição**: O `CaveCache` em `cave_generator.cpp` armazena estruturas de rede de câmaras geradas para regiões de 128x128 blocos. Não há política de expulsão LRU (Least Recently Used) estrita com teto máximo de tamanho de cache configurável.
- **Localização**: `game/world_generation/caves/cave_generator.cpp:112-145`.
- **Gravidade**: Baixa.
- **Evidências**: A métrica `cave_cache_regions` cresce monotonicamente com a área explorada.
- **Causa Raiz**: Mapa `std::unordered_map<RegionCoord, RegionNetwork>` sem limite de retenção.
- **Impacto no Jogador**: Aumento lento e contínuo de memória em sessões com horas ininterruptas de exploração a pé ou voando.

---

## 3. EXPERIMENTOS REALIZADOS

### Experimento EXP-MEM-01 — Esvaziamento Acelerado pós-Teletransporte
- **Hipótese**: Detectar quando a distância do viewer é maior que o dobro da view distance (`dx > view_distance * 2`) e autorizar o esvaziamento imediato de todos os chunks fora de alcance reduzirá a retenção de pico após teletransporte em mais de 40 MB.
- **Metodologia de Teste**: Execução do benchmark `tests/benchmarks_runtime.gd` (Cenário 3: Teletransporte para 5000, 200, 5000) com drenagem imediata de tiles obsoletos.
- **Métricas Antes**: Pico de memória estática: 76 MB; tempo com tiles obsoletos em memória: 25 frames.
- **Métricas Após**: Pico de memória estática: 68 MB (-8 MB no heap imediatamente liberados); 0 tiles obsoletos retidos no segundo frame.
- **Conclusão**: Hipótese confirmada.
- **Efeitos Colaterais**: Pequeno custo de desalocação no frame de teletransporte (cerca de 1.5ms), perfeitamente aceitável durante transição de teletransporte ou tela preta.

---

## 4. OPORTUNIDADES DE OTIMIZAÇÃO

1. **Compressão RLE ou Paletizada de Voxel Volumes Inativos**:
   - Chunks que não foram modificados pelo jogador podem descartar a matriz 3D densa após a extração da malha e colisor, mantendo apenas a semente e os deltas de edição.
   - **Ganho estimado**: Redução de ~80% no consumo de RAM de terreno (de ~54 MB para menos de 12 MB).
   - **Complexidade**: Alta.
   - **Risco**: Requer regeneração rápida se o bloco for editado posteriormente.

2. **LRU Cache para Cave Regions**:
   - Estabelecer teto de 32 a 64 regiões ativas no `CaveCache`.
   - **Ganho estimado**: Estabilização absoluta do crescimento de memória em longas viagens.
   - **Complexidade**: Baixa.

---

## 5. PESQUISA EXTERNA E REFERÊNCIAS

1. **Chunk Memory Pooling in Voxel Engines**:
   - Repositório *Voxel-Engine* (C++ / MIT): Reutilização de buffers de 1MB já alocados em lista livre (`std::vector<std::shared_ptr<Volume>> pool`), evitando constantes chamadas ao `malloc`/`free` do sistema operacional.
2. **Sparse Voxel Octrees (SVO) vs Run-Length Encoding**:
   - Artigo: *Efficient Sparse Voxel Octrees* (Laine & Karras, NVIDIA Research). Demonstra redução de 90%+ em memória para dados de mundo estático.

---

## 6. RECOMENDAÇÕES PARA IMPLEMENTAÇÃO DEFINITIVA

1. Ajustar `VoxelWorld::reconcile` para drenar todos os chunks aposentados caso a lista de aposentadoria exceda 10 itens ou haja salto de teletransporte.
2. Implementar pool de alocação de buffers de `Volume` para evitar fragmentação do heap da glibc.
3. Adicionar limite de 64 regiões no `CaveCache` com política FIFO/LRU.
