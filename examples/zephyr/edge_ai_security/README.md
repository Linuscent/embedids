# Zephyr Edge AI security demo

Simulated inference, rule-based detection and bounded queues on
`native_sim/native/64`. Tested baseline: Zephyr v4.2.0.

See [the integration guide](../../../docs/integrations/zephyr.md) for setup,
commands, scenario acceptance, resource accounting and security limitations.

```bash
west build -b native_sim/native/64 examples/zephyr/edge_ai_security -d build/zephyr-demo
python3 scripts/zephyr/run_scenarios.py --binary build/zephyr-demo/zephyr/zephyr.exe
```

The sample deliberately enables test-only fault commands. Do not copy that
configuration into production firmware.
