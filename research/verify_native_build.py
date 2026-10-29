"""Exercise x64 toolchain discovery with simulated Windows environments."""

from __future__ import annotations

from copy import deepcopy
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import patch


class NativeBuildVerifier:
    def __init__(self, root: Path):
        self.root = root
        spec = importlib.util.spec_from_file_location(
            "runtime_build", root / "build.py"
        )
        self.build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.build)
        self.checks = 0

    def check(self, condition: bool, message: str) -> None:
        if not condition:
            raise ValueError(message)
        self.checks += 1

    def run(self) -> None:
        scratch = self.root / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(
            prefix="native_build_check_", dir=scratch
        ) as name:
            directory = Path(name)
            finder = directory / "Microsoft Visual Studio/Installer/vswhere.exe"
            finder.parent.mkdir(parents=True)
            finder.touch()
            install = directory / "Visual Studio (native)/18/Community"
            setup = install / "VC/Auxiliary/Build/vcvars64.bat"
            setup.parent.mkdir(parents=True)
            setup.touch()
            tools = install / "VC/Tools/MSVC/14.51/bin"
            x64 = str(tools / "Hostx64/x64/cl.exe")
            x86 = str(tools / "HostX86/x86/cl.exe")
            base = {
                "SYSTEMROOT": r"C:\Windows",
                "COMSPEC": r"C:\Windows\System32\cmd.exe",
                "PROGRAMFILES(X86)": str(directory),
                "PATH": "inherited-tools",
                "INCLUDE": "inherited-x86-include",
                "LIB": "inherited-x86-lib",
                "LIBPATH": "inherited-x86-libpath",
                "VSCMD_ARG_TGT_ARCH": "x86",
                "VSCMD_VER": "18.11.0",
                "CL": "/inherited-override",
            }
            configured = "\n".join(
                (
                    "PATH=configured-x64-tools",
                    "INCLUDE=x64-include",
                    "LIB=x64-lib",
                    "VSCMD_ARG_TGT_ARCH=x64",
                    "SYSTEMROOT=C:\\Windows",
                )
            )

            def exercise(initial, incoming, output=configured, returned=x64, code=0):
                original = deepcopy(incoming)

                def which(command, path):
                    if command != "cl.exe":
                        raise ValueError("Unexpected tool lookup")
                    return initial if path == incoming["PATH"] else returned

                completed = subprocess.CompletedProcess(
                    [], code, output, "setup detail"
                )
                with patch.object(
                    self.build.shutil, "which", side_effect=which
                ), patch.object(
                    self.build.subprocess, "check_output", return_value=str(install)
                ), patch.object(
                    self.build.subprocess, "run", return_value=completed
                ) as invoked:
                    try:
                        result = self.build.NativeCompiler.msvc_x64_environment(
                            incoming
                        )
                    except self.build.BuildError:
                        result = None
                self.check(
                    incoming == original,
                    "Compiler discovery mutated its calling environment",
                )
                return result, invoked

            ready = dict(
                base, VSCMD_ARG_TGT_ARCH="x64", INCLUDE="x64-include", LIB="x64-lib"
            )
            result, invoked = exercise(x64, ready)
            self.check(
                result[0] == Path(x64) and not invoked.called,
                "A configured native x64 environment was not reused",
            )
            for initial, env in (
                (x86, base),
                (None, dict(base, INCLUDE="", LIB="")),
                (x64, dict(base, VSCMD_ARG_TGT_ARCH="arm64")),
                (x64, dict(base, VSCMD_ARG_TGT_ARCH="")),
            ):
                result, invoked = exercise(initial, env)
                self.check(
                    result is not None
                    and result[0] == Path(x64)
                    and result[1]["LIB"] == "x64-lib",
                    "Non-x64 or unconfigured discovery did not select x64",
                )
                args, options = invoked.call_args
                command = args[0]
                self.check(
                    isinstance(command, str),
                    "MSVC setup command would be escaped by C-runtime argv conversion",
                )
                prefix = f'"{base["COMSPEC"]}" /d /s /c '
                payload = command.removeprefix(prefix)
                self.check(
                    command.startswith(prefix)
                    and payload[0] == payload[-1] == '"'
                    and payload[1:-1] == f'call "{setup}" >nul && set'
                    and '\\"' not in command,
                    "cmd.exe would receive an incorrectly quoted setup path",
                )
                self.check(
                    str(setup) in command
                    and options["env"]["SYSTEMROOT"] == base["SYSTEMROOT"],
                    "Discovery did not initialize the installed x64 environment",
                )
                self.check(
                    not (
                        {
                            "INCLUDE",
                            "LIB",
                            "LIBPATH",
                            "CL",
                            "VSCMD_VER",
                            "VSCMD_ARG_TGT_ARCH",
                        }
                        & options["env"].keys()
                    ),
                    "Inherited x86/compiler state reached the initialization child",
                )
            broken = subprocess.list2cmdline(
                [base["COMSPEC"], "/d", "/s", "/c", f'call "{setup}" >nul && set']
            )
            self.check(
                '\\"' in broken,
                "Quoting fixture did not reproduce the reported Windows failure",
            )
            if os.name == "nt":
                setup.write_text(
                    '@echo off\nset "TRADING_SETUP_QUOTING=passed"\nexit /b 0\n',
                    encoding="ascii",
                )
                actual = subprocess.run(
                    command, env=options["env"], capture_output=True, text=True
                )
                self.check(
                    actual.returncode == 0
                    and "TRADING_SETUP_QUOTING=passed" in actual.stdout,
                    "Real cmd.exe failed the setup path containing spaces and parentheses",
                )
            for output, returned, code in (
                (configured, x86, 0),
                (configured.replace("LIB=x64-lib", "LIB="), x64, 0),
                (configured, x64, 1),
            ):
                result, _ = exercise(x86, base, output, returned, code)
                self.check(
                    result is None, "An unusable initialized toolchain was accepted"
                )
            finder.unlink()
            result, _ = exercise(x86, base)
            self.check(result is None, "Missing Visual Studio discovery was accepted")
            finder.touch()
            setup.unlink()
            result, _ = exercise(x86, base)
            self.check(result is None, "Missing x64 setup was accepted")
            self.check(
                "panel_account_development"
                in self.build.ProjectSources(self.root).applications(True),
                "The M6 account application is absent from Windows target discovery",
            )
        print(f"Native build discovery checks passed: {self.checks}")
        print("MSVC discovery responses were simulated; no compiler was executed.")
        print(
            "Real cmd.exe quoting check: "
            + ("passed" if os.name == "nt" else "requires Windows")
        )


if __name__ == "__main__":
    try:
        NativeBuildVerifier(Path(__file__).resolve().parents[1]).run()
    except (ValueError, OSError) as error:
        raise SystemExit("Native build verification failed: " + str(error))
