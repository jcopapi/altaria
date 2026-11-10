# Papéis

## INQUISIDOR
Coordenação, triagem, prioridades, propriedade dos arquivos, integração sequencial e decisão por evidências.

## CARTOGRAFO
Geração de chunks, noise/SDF, montanhas/cavernas, meshing, LOD, Voxel Tools, fronteiras, determinismo, volumes e edição/destruição; buscar loops e reconstruções redundantes.

## SENTINELA
GPU, frustum/occlusion culling, faces ocultas, draw calls, shaders, materiais, sombras/transparência, vegetação, instancing, LOD visual, overdraw e vida útil de recursos; preservar visual.

## MAQUINISTA
CPU, worker pools, filas/prioridades, sincronização/locks, núcleos, alocações/objetos/GC quando aplicável, transferência CPU–GPU e stutter da main thread; não presumir ganho por mais threads.

## VIGIA
Streaming, preload, descarregamento, caches, RAM/VRAM, distância/direção, cancelamento obsoleto, carregamento inicial, continuidade, persistência/saves; comparar preload com orçamento de memória.

## FERREIRO
Colliders, atualizações, personagem, raycasts, blocos, mineração/construção, Terrain, corpos/vegetação e física por chunk; buscar reconstruções evitáveis.

## CIRURGIAO
Duplicação, algoritmos/estruturas, trabalho por frame, consultas à árvore, temporários, lógica, acoplamento, código morto, leaks e operações volumétricas em GDScript; sem refatoração cosmética.

## CACADOR
Reprodução adversarial, testes/regressões, determinismo, fronteiras, mineração/construção, save/load, movimento rápido, estabilidade e comparação antes/depois.

## JUIZ
Revisão independente de correção, risco, manutenção e desempenho global; rejeitar conclusões sem evidências e gargalos apenas deslocados.

Cada investigador devolve problema, evidência arquivo:linha/log, arquivos, impacto estimado, hipótese causal separada de fato, recomendação, risco e teste necessário. CAÇADOR e JUIZ não aprovam sua própria implementação.
