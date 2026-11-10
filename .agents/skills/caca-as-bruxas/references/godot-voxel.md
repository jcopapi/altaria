# Godot e Voxel Tools no Altaria
Ponto de partida em 2026-11-10: project.godot declara 4.7/Forward Plus, Jolt e viewport 1280×720. `gdstudio --version` retornou 4.7.1.stable.mono.gdstudio.a450b93bf. `native/dependencies.json` fixa godot-cpp, FastNoise2 1.1.1 e Voxel Tools 1.7.1-dev GDExtension, commit dfb0f81c7fd766f68d78715b2138f4e336cd9737 (API 4.5). Confirme runtime com probe existente antes de inferir incompatibilidade.

Inspecione game/world_generation, game/world_generation/terrain, game/player, native/src, addons/zylann.voxel e JSONs de dados. Existem tests/run_core_tests.sh, run_voxel_tools_tests.sh, run_runtime_tests.sh e benchmarks render/stream. Leia runners antes de executar: alguns compilam, abrem GPU ou modificam saves. Use XDG_DATA_HOME/CONFIG_HOME/CACHE_HOME descartáveis. Não rode setup/build sem avaliar side effects e escopo.

Profiler de scripts não basta para atribuir custo de C++ GDExtension; use instrumentação/medição adequada. Correlacione filas, geração, meshing, uploads, colisões e stalls da main thread. Voxel Tools divide trabalho entre workers e main thread; aumentar workers pode aumentar contenção. Alterar tamanho de bloco afeta draw calls e custo de edição; não aplicar receitas genéricas sem experimento. Dados editáveis e persistentes não podem ser descartados por estarem fora da visão.

Fontes primárias consultadas (2026-11-10; documentação latest pode diferir do commit local):
- https://docs.godotengine.org/en/stable/tutorials/scripting/debug/the_profiler.html
- https://voxel-tools.readthedocs.io/en/latest/performance/
- https://learn.chatgpt.com/docs/build-skills
- https://learn.chatgpt.com/docs/agent-configuration/subagents
- https://github.com/pingdotgg/t3code/blob/main/docs/user/outside-agents.md
- https://github.com/pingdotgg/t3code/blob/main/docs/user/providers-acp.md
