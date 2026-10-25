"""Check the distributed publication without private data or rendering dependencies."""

from __future__ import annotations

import base64
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlparse
import xml.etree.ElementTree as ET


class StaticDocument(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.radio_ids = set()
        self.labels = set()
        self.iframe_count = 0
        self.image_count = 0
        self.child_control_counts = []

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag in ("script", "object", "embed"):
            raise ValueError(f"Executable/unexpected publication element: {tag}")
        if any(key.startswith("on") for key in attrs):
            raise ValueError("Inline event handler in static publication")
        if tag == "input" and attrs.get("type") == "radio":
            self.radio_ids.add(attrs["id"])
        if tag == "label" and "for" in attrs:
            self.labels.add(attrs["for"])
        if tag == "iframe":
            assert "src" not in attrs and "srcdoc" in attrs
            child = StaticDocument()
            child.feed(attrs["srcdoc"])
            assert child.radio_ids <= child.labels
            self.iframe_count += 1
            self.child_control_counts.append(len(child.radio_ids))
        elif tag == "img":
            src = attrs["src"]
            assert src.startswith(
                ("data:image/png;base64,", "data:image/svg+xml;base64,")
            )
            raw = base64.b64decode(src.split(",", 1)[1], validate=True)
            if src.startswith("data:image/svg"):
                ET.fromstring(raw)
            else:
                assert raw.startswith(b"\x89PNG\r\n\x1a\n")
            self.image_count += 1
        elif tag == "link":
            assert attrs.get("rel") != "stylesheet", "External stylesheet dependency"
        elif "src" in attrs:
            assert not attrs["src"].startswith(("http:", "https:", "//"))


class PublicationVerifier:
    def __init__(self, root):
        self.root = Path(root).resolve()

    def verify(self):
        manifest = json.loads(
            (self.root / "docs/publication_manifest.json").read_text()
        )
        for item in manifest["artifacts"]:
            path = (self.root / item["path"]).resolve()
            assert path.is_relative_to(self.root)
            assert (
                hashlib.sha256(path.read_bytes()).hexdigest() == item["sha256"]
            ), item["path"]
        evidence_dir = self.root / "docs/evidence"
        evidence = json.loads((evidence_dir / "manifest.json").read_text())
        for item in evidence["curated_artifacts"]:
            path = (evidence_dir / item["path"]).resolve()
            assert path.is_relative_to(evidence_dir)
            assert hashlib.sha256(path.read_bytes()).hexdigest() == item["sha256"]
        data = json.loads((evidence_dir / "results.json").read_text())
        assert data["later_strategy_prices_opened"] is False
        assert data["reversion"]["sessions"] == data["continuation"]["sessions"] == 476
        assert data["execution"]["case_count"] == 144
        document = StaticDocument()
        document.feed((self.root / "docs/report.html").read_text())
        assert document.iframe_count == 2 and document.image_count >= 8
        assert max(document.child_control_counts) == 216
        assert (self.root / "docs/report.pdf").read_bytes().startswith(b"%PDF-")
        for path in (self.root / "docs/figures").glob("*.svg"):
            svg = ET.fromstring(path.read_text())
            assert svg.tag.endswith("svg") and "viewBox" in svg.attrib
            for node in svg.iter():
                for key, value in node.attrib.items():
                    if key.endswith("href"):
                        assert value.startswith("#"), "External SVG dependency"
        broken = []
        docs = [self.root / "README.md", self.root / "AGENTS.md"]
        docs += list((self.root / "docs").glob("*.md")) + list(
            (self.root / "research").glob("*.md")
        )
        for path in docs:
            for raw in re.findall(r"\[[^\]]*\]\(([^)]+)\)", path.read_text()):
                if urlparse(raw).scheme or raw.startswith("#"):
                    continue
                target = unquote(raw.split("#", 1)[0])
                if not (path.parent / target).exists():
                    broken.append((str(path.relative_to(self.root)), raw))
        assert not broken, broken
        print(
            f"Verified publication: {len(manifest['artifacts'])} hashes, eight SVG figures, "
            "two static embedded views and local document links"
        )


if __name__ == "__main__":
    PublicationVerifier(Path(__file__).resolve().parents[1]).verify()
