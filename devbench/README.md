# DevBench test integration

This directory is compiled only when SCS_WITH_DEVBENCH=ON. Normal Release
builds exclude every file here from SprintCancelsSneak.dll.

DevBenchAPI.h and DevBenchAPI.cpp are the separately MIT-licensed DevBench
consumer API; see DevBenchAPI.LICENSE.txt. The remaining files are test-only
SprintCancelsSneak infrastructure and are not covered by that MIT grant.
