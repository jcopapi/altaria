# Fases A–G e portas de aceitação
A — Reconhecimento: versão real do engine, renderer, build debug/release, revisão das dependências (inclusive Voxel Tools), cenas/sistemas, testes, bugs conhecidos, hardware/driver e limites do ambiente. Distinga versão declarada da executada. Se Git indisponível, use hashes; não declare árvore limpa.

B — Baseline: antes de alterar código, fixe seed, cena, snapshots de geração/biomas, save descartável, resolução interna/externa, câmera/percurso por tempo, duração, warmup, VSync/FPS cap, qualidade, build, engine/driver, hardware e carga de fundo. Guarde manifesto e logs/amostras brutas. Repita pelo menos 3 vezes quando viável e separe execução fria/quente. Meça frame time mediana/p95/p99/máximo e stutters, FPS, CPU/GPU, RAM/VRAM, objetos, filas/chunks, uploads/colliders conforme hipótese. Indique fonte/unidade/limitação; métrica ausente não é zero. Headless não valida GPU/visual. Timeout/erros invalidam amostras, mesmo com PASS.

C — Investigação: tarefas independentes delimitadas, evidências concretas e formato de descoberta. Distinguir bug reproduzido, limitação estática e hipótese; não inferir gargalo do tamanho do arquivo.

D — Prioridade: impacto, frequência, gravidade, risco de regressão, complexidade e confiança; registre justificativa, dependências e proprietário por caminho.

E — Implementação: patches pequenos, reversíveis e mensuráveis; um fator por experimento. Registre diff, build e instrução de rollback sem apagar trabalho alheio.

F — Contra-auditoria: CAÇADOR testa determinismo e fronteiras, mineração/construção, equipamento, save/load/restart, viagem rápida, cancelamento, colisões e estabilidade conforme superfície afetada. JUIZ examina correção e custo em outros subsistemas; comparação visual em posições fixas. Revisores independentes do implementador.

G — Benchmark final: repetir manifesto B com instrumentação idêntica e mesma carga; mudança de cenário torna comparação inconclusiva. Compare distribuição por execução e variabilidade, não apenas média/FPS. Defina antecipadamente tolerâncias para ruído, memória e regressões. Só aceitar ganho além do ruído com invariantes preservados ou bug demonstravelmente corrigido. Resultado inconclusivo não é ganho. Registre rejeição/reversão e pendências.
