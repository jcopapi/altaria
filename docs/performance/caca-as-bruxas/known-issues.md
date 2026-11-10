# Histórico de problemas conhecidos

| ID | Estado | Evidência | Próxima ação |
|---|---|---|---|
| CAB-001 | Aberto; limitação estática confirmada | tests/render_benchmark.gd:5–12,53–54 e tests/stream_benchmark.gd:32,62–63: timeout não impede PASS | Reproduzir timeout isolado; exigir falha e descartar amostras antes de corrigir |
| CAB-002 | Hipótese de viés | tests/stream_benchmark.gd:36–40 move por frame | Comparar percurso por tempo; não alegar gargalo real |

Para cada atualização registre operação, revisão/hash, reprodução, responsável, decisão e condição para reabrir. Não reabrir investigação resolvida sem nova evidência.
