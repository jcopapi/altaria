# Caça às Bruxas — auditorias permanentes

Skill: `.agents/skills/caca-as-bruxas/SKILL.md`. Invocação: `$caca-as-bruxas investigue os travamentos de streaming, começando somente pela auditoria`.
Se a skill recém-criada não aparecer, abra uma nova conversa no projeto; como alternativa, peça explicitamente para ler o SKILL.md pelo caminho.

Configuração editável em `.agents/skills/caca-as-bruxas/config/roles.json`. Os papéis são perfis de tarefas para subagentes reais; não representam nove serviços residentes. O coordenador lê a configuração a cada operação; ela não altera configurações globais nem instala provedores.

Criar operação:
```bash
python3 .agents/skills/caca-as-bruxas/scripts/audit.py init . 2026-11-10-streaming
```
Resumir amostras exportadas com coluna `frame_ms`:
```bash
python3 .agents/skills/caca-as-bruxas/scripts/audit.py summarize /caminho/frames.csv
```
Esse utilitário não captura métricas do engine. Leia o protocolo da skill para cenários e instrumentação. Não tratar CSV sintético como ganho real.

Operações usam report.md com reconhecimento, baseline, descobertas, plano, alterações, testes, benchmark final e pendências; raw/ guarda logs e amostras; handoffs/ guarda intercâmbio externo. Consulte known-issues.md antes de investigar.

## Capacidades verificadas em 2026-11-10
- Codex CLI 0.167.0; ferramenta de subagentes nativa executou e devolveu resultado real nesta sessão. Todos os oito especialistas podem receber tarefas como perfis Codex, sob o INQUISIDOR. Nem todos foram executados nesta entrega.
- 4 slots totais no runtime desta sessão; configuração conservadora de até 2 workers + coordenador. Revalidar em outra sessão.
- T3 Code Beta instalado como AppImage; desktop entry declara 0.1.7 (não foi interrogado o processo em execução). Settings contém chaves de provedores codex e antigravity; isso não confirma autenticação/modelos disponíveis. Não foram lidas credenciais.
- Claude Code, OpenCode e t3/t3-code não encontrados no PATH. Isso não exclui instalações encapsuladas no T3.
- Sem ferramenta T3 MCP exposta nesta sessão; sem round trip externo testado. Handoff por arquivos implementado; transferência exige operador ou integração adicional.
- Documentação T3 atual descreve MCP externo/ACP e delegação, mas não confirma a capacidade da instalação local. Integração automática permanece pendente de conexão, descoberta e teste real.
- Git não funcional neste ambiente: .git aparece vazia/protegida e git status falha. Não foi criada branch/worktree nem substituída configuração. Operações de escrita devem usar snapshots/diffs e serialização até Git estar disponível.

Fontes consultadas: [skills Codex](https://learn.chatgpt.com/docs/build-skills), [subagentes](https://learn.chatgpt.com/docs/agent-configuration/subagents), [MCP externo T3](https://github.com/pingdotgg/t3code/blob/main/docs/user/outside-agents.md), [provedores ACP](https://github.com/pingdotgg/t3code/blob/main/docs/user/providers-acp.md). Os dois primeiros sustentam formato/descoberta e delegação; os últimos descrevem possibilidades, não uma integração local testada.
