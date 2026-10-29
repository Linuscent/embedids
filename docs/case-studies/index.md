---
title: Case Studies
schema_type: CollectionPage
description: How EmbedIDS is applied to real embedded and edge security problems, with measured results.
legacyAnchors: [case-studies]
---

How EmbedIDS is applied to concrete embedded and edge security problems, with
the measurements to back it up.

## Runtime security for Edge AI devices

![EmbedIDS EdgeML case study: runtime security for Edge AI devices](../assets/case-studies/edgeml/hero.svg)

EmbedIDS EdgeML detects rogue GPU workloads, AI model tampering and
model exfiltration on a Jetson-class vision-inference device. It flags
model tampering in 1 s and runs in about 9 MB of memory.

[Read the Edge AI runtime security case study](edgeml-runtime-security.mdx)

## Runtime security on Zephyr RTOS

![EmbedIDS Zephyr case study: runtime security inside the firmware](../assets/case-studies/zephyr/hero.svg)

EmbedIDS runs inside Zephyr firmware as an optional module, with a bounded
sample queue and one detector thread. In simulation it flags command abuse,
request floods, tampering and telemetry loss within 120 ms, using 640 bytes
of metric history.

[Read the Zephyr runtime security case study](zephyr-runtime-security.mdx)
