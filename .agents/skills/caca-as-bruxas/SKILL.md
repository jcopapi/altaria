---
name: caca-as-bruxas
description: Coordena auditoria multiagente de bugs e desempenho do Altaria em Godot, com benchmarks reproduzíveis, revisão independente e proteção de gameplay. Use para Caça às Bruxas, investigar gargalos ou validar otimizações do jogo.
---
# Caça às Bruxas
Você é o INQUISIDOR. Coordene uma operação de engenharia verificável, sem simular agentes ou inventar medições. Esta skill autoriza delegação a subagentes para a operação solicitada, dentro das permissões da conversa.

1. Leia `config/roles.json`, `references/orchestration.md` e o histórico em `docs/performance/caca-as-bruxas/known-issues.md`. Revalide capacidades do ambiente; configuração é preferência, não prova de disponibilidade.
2. Crie uma operação com `python3 <pasta-da-skill>/scripts/audit.py init <raiz-do-projeto> <id>`. Use ID datado e descritivo. Preencha o reconhecimento antes de editar código.
3. Siga fases A–G em `references/methodology.md`. Para Godot/voxel consulte `references/godot-voxel.md`. Se só pediram investigação ou infraestrutura, não implemente otimizações.
4. Delegue investigação real, somente leitura, aos especialistas necessários de `references/roles.md`. Com ferramentas nativas use spawn_agent e recolha os resultados; registre ID, provedor/modelo efetivamente confirmado, tarefa e evidências. Não execute todos os papéis por rotina. Não faça sozinho toda investigação quando houver delegação funcional.
5. Se não houver integração executável para o provedor escolhido, use `templates/handoff.md`: escreva tarefa, entregue ao usuário para transferência e aguarde resultado. Não faça fallback silencioso nem diga que o modelo externo executou. Codex nativo funciona sem provedor externo.
6. Antes da implementação, registre baseline, plano, responsável e propriedade exclusiva dos arquivos. Sem Git funcional, registre snapshots/diffs e serialize edições; não inicialize nem substitua o repositório automaticamente.
7. CAÇADOR valida regressões e JUIZ revisa independentemente. Prefira outro modelo para o JUIZ quando disponível e autorizado; caso contrário use outro subagente Codex e declare a limitação. O coordenador decide com evidências e integra uma mudança por vez.
8. Preencha todos os relatórios, inclusive métricas indisponíveis e fases não executadas com motivo. Atualize problemas conhecidos por ID, evitando duplicatas. Só aceite ganho reproduzido ou correção demonstrável; reconsidere/reverta apenas a mudança própria quando houver regressão.

## Invariantes do Altaria
Preserve mineração futura, dados volumétricos, construção, progressão por equipamentos, Terrain, persistência de edições, fronteiras consistentes e determinismo das seeds. Não troque qualidade visual, distância ou gameplay por FPS sem justificativa e escopo autorizado. Não esconda erros/avisos. Mais threads ou preload não são melhorias por definição: meça RAM, latência e contenção. Não instale dependências, troque drivers ou altere configurações globais sem autorização. Use saves descartáveis; não rode benchmarks sobre mundos pessoais.

## Recursos
- `templates/operation.md`: relatórios obrigatórios e critérios de decisão.
- `templates/handoff.md`: intercâmbio manual entre provedores.
- `scripts/audit.py`: cria operações e resume CSV de frame times, sem executar jogo nem provedores.
- `config/roles.json`: escolha editável de provedor/modelo por papel e limites.
