"""
PlatformIO pre-script guard for native app environments.
Prevents standard `pio run -e app-* -t upload` from accidentally flashing
over the factory launcher partition (0x10000).
"""
Import("env")
import sys

# Block direct upload/erase targets on native app environments
COMMAND_LINE_TARGETS = env.get("COMMAND_LINE_TARGETS", [])
if any(t in ("upload", "uploadfs", "erase") for t in COMMAND_LINE_TARGETS):
    sys.stderr.write(
        "\n[CARDPUTER-OS GUARD] Refusing direct upload for native app environment '%s'.\n"
        "Native apps must be packaged and installed into ota_0 (0x210000) via the\n"
        "factory launcher or an explicit ota_0 flash command to avoid overwriting\n"
        "the factory partition at 0x10000.\n" % env["PIOENV"]
    )
    env.Exit(1)
