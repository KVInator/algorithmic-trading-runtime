"""Build the C++20 applications with MSVC or GCC/Clang, without CMake."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request


class BuildError(RuntimeError):
    pass


class BuildLock:
    """Hold an OS-owned byte lock; interrupted builds leave no stale ownership."""

    def __init__(self, path: Path):
        self.path = path
        self.stream = None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.stream = self.path.open("a+b")
        if self.path.stat().st_size == 0:
            self.stream.write(b"0")
            self.stream.flush()
        self.stream.seek(0)
        try:
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            self.stream.close()
            raise BuildError(f"Another build owns {self.path}") from error
        return self

    def __exit__(self, *_):
        self.stream.close()


class ProjectSources:
    """Own the source inventory and exact registered protocol identities."""

    frozen = {
        "extension_protocol.json": "269d6e677a3e62a21ede6e88659edb2f7a998bfe0137e62b13458e411f0c55ee",
        "intraday_momentum_protocol.md": "b013e488f56a3b3dd2d6d53c1226c63d1d24e21e300b84ce538312f5aacf9499",
        "development_data_policy.json": "34dda94bf3adfc065bf739ec19296e6fae1f78b3830f061eb4938fb011490349",
        "intraday_continuation_protocol.md": "b08cb6bf8acc955d78e2c8eec5b633b59aa779e16657ce04c200a27751981ba9",
        "daily_risk_protocol.md": "28e935174043745098b3f53d7eb71669fe54c8fc6d510a68c568da28874c8a3a",
        "daily_risk_protocol.json": "3a37f24f29e661c606c32af81b3daf5b5e909520aaaeaf5eafe7efd0340f51d2",
        "intraday_reversion_protocol.md": "1fa24089060ccfc82963ee53216927fb0fa9e0f10f02948eae5b72b4032207b9",
        "intraday_reversion_protocol.json": "64640f797613466b43b6c0c8686f317aa6a5bcc5425985e931816a6910e8ff86",
    }

    def __init__(self, root: Path):
        self.root = root

    @staticmethod
    def digest(data: bytes) -> str:
        return hashlib.sha256(data).hexdigest()

    def prepare_protocols(self) -> None:
        # Checkout newline conversion must not change registered input identities.
        for name, expected in self.frozen.items():
            path = self.root / "research" / name
            raw = path.read_bytes()
            if self.digest(raw) == expected:
                continue
            canonical = raw.replace(b"\r\n", b"\n")
            if self.digest(canonical) != expected:
                raise BuildError(f"Registered protocol content differs: {name}")
            path.write_bytes(canonical)

    def inventory(self) -> tuple[str, str]:
        files = []
        for folder in ("src", "apps", "research"):
            files.extend(p for p in (self.root / folder).rglob("*") if p.is_file())
        files.extend(
            self.root / name for name in ("build.py", "Runtime.sln", "Runtime.vcxproj")
        )
        files = [
            p
            for p in files
            if p.suffix
            in {
                ".cpp",
                ".hpp",
                ".py",
                ".md",
                ".json",
                ".csv",
                ".txt",
                ".sln",
                ".vcxproj",
            }
        ]
        records = "".join(
            f"{p.relative_to(self.root).as_posix()}:{self.digest(p.read_bytes())}\n"
            for p in sorted(files, key=lambda p: p.relative_to(self.root).as_posix())
        )
        return records, self.digest(records.encode("utf-8"))

    def core(self, windows: bool) -> list[Path]:
        excluded = {"posix", "openssl"} if windows else {"windows"}
        return sorted(
            p
            for p in (self.root / "src").rglob("*.cpp")
            if not (p.parent.parent.name == "platform" and p.parent.name in excluded)
        )

    def applications(self, windows: bool) -> dict[str, Path]:
        return {
            p.stem: p
            for p in sorted((self.root / "apps").glob("*.cpp"))
            if windows or p.stem != "alpaca_development"
        }


class JsonDependency:
    version = "3.12.0"
    archive_sha256 = "42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa"
    url = "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"

    def __init__(self, root: Path, override: Path | None):
        self.directory = root / "scratch/dependencies/json-3.12.0"
        self.override = override

    @staticmethod
    def validate(include: Path) -> Path:
        macros = include / "nlohmann/detail/abi_macros.hpp"
        if not (include / "nlohmann/json.hpp").is_file() or not macros.is_file():
            raise BuildError(f"Missing nlohmann/json headers: {include}")
        text = macros.read_text(encoding="utf-8")
        for name, number in (("MAJOR", 3), ("MINOR", 12), ("PATCH", 0)):
            if not re.search(
                rf"#\s*define\s+NLOHMANN_JSON_VERSION_{name}\s+{number}\b", text
            ):
                raise BuildError("The build requires nlohmann/json 3.12.0")
        return include

    def prepare(self) -> Path:
        if self.override:
            return self.validate(self.override.resolve())
        with BuildLock(self.directory.parent / "dependency.lock"):
            marker = self.directory / "archive.sha256"
            if marker.is_file() and marker.read_text().strip() == self.archive_sha256:
                return self.validate(self.directory / "include")
            self.directory.mkdir(parents=True, exist_ok=True)
            archive = self.directory / "json.tar.xz"
            print("Acquiring pinned nlohmann/json 3.12.0", flush=True)
            with urllib.request.urlopen(self.url, timeout=90) as response:
                data = response.read(16 * 1024 * 1024 + 1)
            if (
                len(data) > 16 * 1024 * 1024
                or ProjectSources.digest(data) != self.archive_sha256
            ):
                raise BuildError("JSON dependency archive digest differs")
            archive.write_bytes(data)
            with tarfile.open(archive, "r:xz") as package:
                headers = [
                    m
                    for m in package.getmembers()
                    if m.name.endswith("/include/nlohmann/json.hpp")
                ]
                if len(headers) != 1:
                    raise BuildError("Unexpected JSON archive layout")
                prefix = headers[0].name.removesuffix("include/nlohmann/json.hpp")
                for member in package.getmembers():
                    if (
                        not member.name.startswith(prefix + "include/")
                        or member.isdir()
                    ):
                        continue
                    if not member.isfile():
                        raise BuildError("Unexpected link in JSON dependency")
                    relative = Path(member.name.removeprefix(prefix))
                    destination = (self.directory / relative).resolve()
                    if not destination.is_relative_to(self.directory.resolve()):
                        raise BuildError("Dependency path escapes its directory")
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with package.extractfile(member) as source:
                        destination.write_bytes(source.read())
            include = self.validate(self.directory / "include")
            marker.write_text(self.archive_sha256 + "\n", encoding="ascii")
            return include


class NativeCompiler:
    def __init__(
        self,
        executable: Path,
        environment: dict[str, str],
        windows: bool,
        configuration: str,
    ):
        self.executable = executable
        self.environment = environment
        self.windows = windows
        self.configuration = configuration
        probe = self.command(
            [str(executable), "/Bv" if windows else "--version"], check=False
        )
        if windows:
            match = re.search(r"\b19\.\d+\.\d+(?:\.\d+)?", probe)
            if not match:
                raise BuildError("Unable to identify the MSVC compiler")
            self.identity = "MSVC " + match.group()
            self.common = [
                "/nologo",
                "/std:c++20",
                "/EHsc",
                "/W4",
                "/WX",
                "/permissive-",
                "/utf-8",
            ]
            self.options = (
                ["/O2", "/Ob2", "/DNDEBUG", "/MD"]
                if configuration == "Release"
                else ["/Od", "/Z7", "/MDd"]
            )
        else:
            version = self.command(
                [str(executable), "-dumpfullversion", "-dumpversion"]
            ).strip()
            self.identity = ("Clang " if "clang" in probe.lower() else "GNU ") + version
            self.common = [
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Wconversion",
                "-Wshadow",
                "-Werror",
                "-pthread",
            ]
            self.options = (
                ["-O3", "-DNDEBUG"] if configuration == "Release" else ["-O0", "-g"]
            )

    @classmethod
    def discover(cls, configuration: str):
        windows = os.name == "nt"
        environment = {
            key.upper() if windows else key: value for key, value in os.environ.items()
        }
        if windows:
            if not shutil.which("cl.exe"):
                program_files = Path(
                    environment.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")
                )
                finder = program_files / "Microsoft Visual Studio/Installer/vswhere.exe"
                if not finder.is_file():
                    raise BuildError("Visual Studio C++ tools are required")
                install = subprocess.check_output(
                    [
                        str(finder),
                        "-latest",
                        "-products",
                        "*",
                        "-requires",
                        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                        "-property",
                        "installationPath",
                    ],
                    text=True,
                ).strip()
                setup = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
                if not setup.is_file():
                    raise BuildError("MSVC x64 environment script is missing")
                result = subprocess.run(
                    ["cmd.exe", "/d", "/s", "/c", f'call "{setup}" >nul && set'],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                for line in result.stdout.splitlines():
                    key, separator, value = line.partition("=")
                    if separator and key:
                        environment[key.upper()] = value
            executable = shutil.which("cl.exe", path=environment.get("PATH"))
        elif sys.platform.startswith("linux"):
            executable = shutil.which(environment.get("CXX", "g++"))
        else:
            raise BuildError("Supported build platforms are Windows and Linux")
        if not executable:
            raise BuildError("A C++20 compiler was not found")
        return cls(Path(executable), environment, windows, configuration)

    def command(self, arguments: list[str], check: bool = True) -> str:
        result = subprocess.run(
            arguments,
            env=self.environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        output = result.stdout + result.stderr
        if check and result.returncode:
            raise BuildError(
                f"Command failed: {subprocess.list2cmdline(arguments)}\n{output}"
            )
        return output


class ApplicationBuild:
    """Compile one shared archive and the supported thin application entry points."""

    def __init__(
        self, root: Path, configuration: str, jobs: int, json_include: Path | None
    ):
        self.root = root
        self.configuration = configuration
        self.jobs = jobs
        self.sources = ProjectSources(root)
        self.dependency = JsonDependency(root, json_include)
        self.directory = root / "scratch/native" / configuration
        self.compiler = None

    @staticmethod
    def write(path: Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)

    def metadata(self) -> None:
        records, source_digest = self.sources.inventory()
        values = {
            "TRADING_PROJECT_ROOT": self.root.as_posix(),
            "TRADING_DATA_POLICY_SHA256": self.sources.frozen[
                "development_data_policy.json"
            ],
            "TRADING_PROTOCOL_SHA256": self.sources.frozen[
                "intraday_momentum_protocol.md"
            ],
            "TRADING_CONTINUATION_PROTOCOL_SHA256": self.sources.frozen[
                "intraday_continuation_protocol.md"
            ],
            "TRADING_RELEASE_CALENDAR_SHA256": ProjectSources.digest(
                (self.root / "research/scheduled_releases_2022_2023.csv").read_bytes()
            ),
            "TRADING_EXTENSION_PROTOCOL_SHA256": self.sources.frozen["extension_protocol.json"],
            "TRADING_SOURCE_SHA256": source_digest,
            "TRADING_COMPILER_ID": self.compiler.identity,
            "TRADING_BUILD_COMPILER": self.compiler.identity.replace(" ", "-"),
            "TRADING_BUILD_CONFIGURATION": self.configuration,
        }
        generated = self.directory / "generated"
        text = "#pragma once\n"
        if self.compiler.windows:
            for name, value in (
                ("WIN32_LEAN_AND_MEAN", "1"),
                ("NOMINMAX", "1"),
                ("_WIN32_WINNT", "0x0A00"),
            ):
                text += f"#ifndef {name}\n#define {name} {value}\n#endif\n"
        text += "".join(
            f"#define {key} {json.dumps(value)}\n" for key, value in values.items()
        )
        self.write(generated / "trading_build_config.hpp", text.encode())
        self.write(
            generated / "trading_source_inventory.hpp",
            (
                "#pragma once\n#include <string_view>\nnamespace trading::build {\n"
                f'inline constexpr std::string_view source_sha256{{"{source_digest}"}};\n'
                f'inline constexpr std::string_view source_records{{R"inventory({records})inventory"}};\n}}\n'
            ).encode(),
        )
        self.write(
            generated / "runtime_benchmark_build.hpp",
            (
                "#pragma once\n#include <string_view>\nnamespace trading::build {\n"
                f'inline constexpr std::string_view benchmark_common_flags{{{json.dumps(" ".join(self.compiler.common))}}};\n'
                f'inline constexpr std::string_view benchmark_configuration_flags{{{json.dumps(" ".join(self.compiler.options))}}};\n'
                "inline constexpr bool benchmark_warnings_as_errors{true};\n}\n"
            ).encode(),
        )
        self.write(self.directory / "source_inventory.txt", records.encode())

    def compile(self, source: Path) -> Path:
        suffix = ".obj" if self.compiler.windows else ".o"
        output = (
            self.directory / "objects" / source.relative_to(self.root)
        ).with_suffix(suffix)
        stamp = output.with_suffix(output.suffix + ".sha256")
        signature = ProjectSources.digest(self.header_identity + source.read_bytes())
        if (
            output.is_file()
            and stamp.is_file()
            and stamp.read_text().strip() == signature
        ):
            return output
        output.parent.mkdir(parents=True, exist_ok=True)
        flags = [
            str(self.compiler.executable),
            *self.compiler.common,
            *self.compiler.options,
        ]
        if self.compiler.windows:
            flags += [f"/I{p}" for p in self.includes] + [
                f"/FI{self.directory / 'generated/trading_build_config.hpp'}",
                "/c",
                str(source),
                f"/Fo{output}",
            ]
        else:
            flags += [f"-I{p}" for p in self.includes] + [
                "-include",
                str(self.directory / "generated/trading_build_config.hpp"),
                "-c",
                str(source),
                "-o",
                str(output),
            ]
        self.compiler.command(flags)
        stamp.write_text(signature + "\n", encoding="ascii")
        return output

    def run(self, target: str) -> Path:
        self.sources.prepare_protocols()
        self.compiler = NativeCompiler.discover(self.configuration)
        applications = self.sources.applications(self.compiler.windows)
        if target != "all" and target not in applications:
            raise BuildError(
                f"Unknown application: {target}; available: {', '.join(applications)}"
            )
        selected = applications if target == "all" else {target: applications[target]}
        with BuildLock(self.directory / "build.lock"):
            include = self.dependency.prepare()
            self.metadata()
            self.includes = sorted(
                {p.parent for p in (self.root / "src").rglob("*.hpp")}
            ) + [include, self.directory / "generated"]
            headers = (
                sorted((self.root / "src").rglob("*.hpp"))
                + sorted((self.root / "apps").glob("*.hpp"))
                + sorted((self.directory / "generated").glob("*.hpp"))
            )
            self.header_identity = hashlib.sha256(
                (
                    str(self.compiler.executable)
                    + self.compiler.identity
                    + str(self.compiler.common + self.compiler.options)
                    + str(self.includes)
                    + self.compiler.environment.get("INCLUDE", "")
                    + self.compiler.environment.get("LIB", "")
                ).encode()
            )
            for header in headers:
                self.header_identity.update(
                    header.relative_to(self.root).as_posix().encode()
                    + header.read_bytes()
                )
            self.header_identity = self.header_identity.digest()
            core = self.sources.core(self.compiler.windows)
            units = core + list(selected.values())
            print(
                f"{self.compiler.identity} | {self.configuration} | {len(selected)} applications",
                flush=True,
            )
            objects = {}
            with ThreadPoolExecutor(max_workers=self.jobs) as pool:
                futures = {
                    pool.submit(self.compile, source): source for source in units
                }
                for count, future in enumerate(as_completed(futures), 1):
                    source = futures[future]
                    objects[source] = future.result()
                    if count == 1 or count % 8 == 0 or count == len(units):
                        print(f"Compiled or current: {count}/{len(units)}", flush=True)
            library = self.directory / (
                "trading_runtime.lib"
                if self.compiler.windows
                else "libtrading_runtime.a"
            )
            signature = ProjectSources.digest(
                b"".join(
                    objects[p].with_suffix(objects[p].suffix + ".sha256").read_bytes()
                    for p in core
                )
            )
            stamp = self.directory / "library.sha256"
            if (
                not library.is_file()
                or not stamp.is_file()
                or stamp.read_text().strip() != signature
            ):
                temporary = library.with_name("building-" + library.name)
                temporary.unlink(missing_ok=True)
                if self.compiler.windows:
                    librarian = self.compiler.executable.with_name("lib.exe")
                    command = [str(librarian), "/nologo", f"/OUT:{temporary}"]
                else:
                    librarian = shutil.which("ar")
                    if not librarian:
                        raise BuildError("The archive tool ar was not found")
                    command = [librarian, "rcs", str(temporary)]
                self.compiler.command(command + [str(objects[p]) for p in core])
                temporary.replace(library)
                stamp.write_text(signature + "\n", encoding="ascii")
            for name, source in selected.items():
                executable = self.directory / (
                    name + (".exe" if self.compiler.windows else "")
                )
                if executable.exists() and executable.stat().st_mtime_ns >= max(
                    library.stat().st_mtime_ns, objects[source].stat().st_mtime_ns
                ):
                    continue
                print(f"Linking {name}", flush=True)
                pending = executable.with_name("linking-" + executable.name)
                pending.unlink(missing_ok=True)
                if self.compiler.windows:
                    command = [
                        str(self.compiler.executable),
                        "/nologo",
                        str(objects[source]),
                        str(library),
                        f"/Fe{pending}",
                        "/link",
                        "/INCREMENTAL:NO",
                        "bcrypt.lib",
                        "advapi32.lib",
                        "winhttp.lib",
                        "psapi.lib",
                    ]
                    if self.configuration == "Debug":
                        command += ["/DEBUG", f"/PDB:{executable.with_suffix('.pdb')}"]
                else:
                    command = [
                        str(self.compiler.executable),
                        str(objects[source]),
                        str(library),
                        "-pthread",
                        "-lcrypto",
                        "-o",
                        str(pending),
                    ]
                self.compiler.command(command)
                pending.replace(executable)
            print(f"Applications: {self.directory}", flush=True)
        return self.directory / (
            (target if target != "all" else "project_demo")
            + (".exe" if self.compiler.windows else "")
        )

    def clean(self) -> None:
        if self.directory.exists():
            with BuildLock(self.directory / "build.lock"):
                for path in self.directory.iterdir():
                    if path.name == "build.lock":
                        continue
                    if path.is_dir():
                        shutil.rmtree(path)
                    else:
                        path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--configuration", choices=("Debug", "Release"), default="Release"
    )
    parser.add_argument("--target", default="all", help="Application name, or all")
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument(
        "--json-include",
        type=Path,
        help="Existing nlohmann/json 3.12.0 include directory",
    )
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    project = ApplicationBuild(
        Path(__file__).resolve().parent,
        args.configuration,
        args.jobs,
        args.json_include,
    )
    try:
        if args.clean or args.rebuild:
            project.clean()
        if not args.clean:
            executable = project.run(args.target)
            if args.run:
                return subprocess.call([str(executable)])
    except (BuildError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
