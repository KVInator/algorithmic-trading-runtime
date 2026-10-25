"""Build the publication from curated evidence; no signal, account or historical replay."""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import html
import json
from pathlib import Path
import re
import statistics
import textwrap

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from reportlab import rl_config
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.utils import ImageReader
from reportlab.platypus import (
    SimpleDocTemplate,
    Paragraph,
    Spacer,
    PageBreak,
    Image,
    Table,
    TableStyle,
    KeepTogether,
    Preformatted,
)


class Evidence:
    """Read the distributed snapshot and verify its recorded artifact identities."""

    def __init__(self, root: Path):
        self.root = root
        directory = root / "docs/evidence"
        self.manifest = json.loads((directory / "manifest.json").read_text())
        for record in self.manifest["curated_artifacts"]:
            path = (directory / record["path"]).resolve()
            if not path.is_relative_to(directory.resolve()):
                raise ValueError("Evidence path escapes its directory")
            if hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
                raise ValueError(f'Evidence digest differs: {record["path"]}')
        self.results = json.loads((directory / "results.json").read_text())
        self.runtime = {}
        for platform in ("windows", "linux"):
            with (directory / f"runtime_{platform}.csv").open(newline="") as stream:
                self.runtime[platform] = list(csv.DictReader(stream))


class Figures:
    """Publication transforms only: units, grouping and descriptive display summaries."""

    teal, blue, red, gold = "#137e87", "#325b9b", "#b8443b", "#bc8628"

    def __init__(self, evidence: Evidence, temporary: Path):
        self.data = evidence.results
        self.runtime = evidence.runtime
        self.destination = evidence.root / "docs/figures"
        self.temporary = temporary
        self.paths = {}

    def save(self, name, fig):
        fig.tight_layout(pad=1.4)
        svg, png = self.destination / f"{name}.svg", self.temporary / f"{name}.png"
        fig.savefig(
            svg, metadata={"Date": None}, bbox_inches="tight", facecolor="white"
        )
        fig.savefig(png, dpi=180, bbox_inches="tight", facecolor="white")
        plt.close(fig)
        self.paths[name] = (svg, png)

    def render(self):
        self.destination.mkdir(parents=True, exist_ok=True)
        self.temporary.mkdir(parents=True, exist_ok=True)
        plt.rcParams.update(
            {
                "font.family": "DejaVu Sans",
                "font.size": 12,
                "axes.spines.top": False,
                "axes.spines.right": False,
                "axes.labelcolor": "#263c50",
                "text.color": "#183249",
                "axes.grid": True,
                "grid.color": "#e5ebef",
                "axes.axisbelow": True,
                "svg.fonttype": "none",
                "svg.hashsalt": "algorithmic-trading-runtime-report",
            }
        )

        self.historical()
        self.risk()
        self.noise()
        self.lifecycle()
        self.policy()
        self.quotes()
        self.processing()
        self.recovery()

    def historical(self):
        fig, axes = plt.subplots(1, 3, figsize=(13, 4.2))
        im, tod, mr = (self.data[k] for k in ("momentum", "continuation", "reversion"))
        groups = [
            (
                "Late momentum",
                ["1m / 0 bp", "1m / 1 bp", "2m / 1 bp"],
                [x["return_pct"] for x in im["cases"]],
            ),
            (
                "Volume continuation",
                ["Full sample", "Five winners\nreplaced by cash"],
                [
                    tod["volume_primary_return_pct"],
                    tod["volume_without_five_winners_return_pct"],
                ],
            ),
            (
                "Opening-anchor reversion",
                ["Gross", "1 bp per side"],
                [mr["gross_return_pct"], mr["net_return_pct"]],
            ),
        ]
        for ax, (title, labels, values) in zip(axes, groups):
            ax.bar(
                labels,
                values,
                color=[self.teal if x > 0 else self.red for x in values],
                width=0.6,
            )
            ax.axhline(0, color="#344b5d", linewidth=0.8)
            ax.set_title(title, fontsize=12)
            ax.set_ylabel("Compounded return (%)")
            ax.tick_params(axis="x", labelsize=10)
            ax.margins(y=0.25)
            for i, v in enumerate(values):
                ax.annotate(
                    f"{v:+.2f}%",
                    (i, v),
                    xytext=(0, 5 if v >= 0 else -16),
                    textcoords="offset points",
                    ha="center",
                    fontsize=10,
                )
        self.save("research_findings", fig)

    def risk(self):
        r = self.data["risk"]
        fig, axes = plt.subplots(1, 2, figsize=(11, 4))
        keys = [
            ("cer_over_daily_95", "cer_over_daily", "CER over daily-only"),
            ("cer_over_static_95", "cer_over_static", "CER over static"),
        ]
        for i, (interval, point, label) in enumerate(keys):
            for j, b in enumerate(r["bootstrap"]):
                x = r["primary"][point] * 100
                ci = b[interval]
                y = i + (j - 0.5) * 0.2
                axes[0].errorbar(
                    x,
                    y,
                    xerr=[[x - ci["lower"] * 100], [ci["upper"] * 100 - x]],
                    fmt="o",
                    capsize=4,
                    color=[self.teal, self.blue][j],
                    label=f'Block {b["block_length"]}' if i == 0 else None,
                )
        axes[0].set_yticks([0, 1], [x[2] for x in keys])
        axes[0].axvline(0, color=self.red, lw=1)
        axes[0].set_xlabel("CER increment (percentage points/year)")
        axes[0].legend(fontsize=10)
        for i, b in enumerate(r["bootstrap"]):
            x = r["primary"]["qlike_increment"]
            ci = b["qlike_increment_95"]
            axes[1].errorbar(
                x,
                i,
                xerr=[[x - ci["lower"]], [ci["upper"] - x]],
                fmt="o",
                capsize=4,
                color=[self.teal, self.blue][i],
            )
        axes[1].set_yticks([0, 1], ["Block 20", "Block 40"])
        axes[1].axvline(0, color=self.red, lw=1)
        axes[1].set_xlabel("Daily-only loss minus intraday-risk loss")
        axes[1].set_title("Paired QLIKE improvement", fontsize=12)
        self.save("risk_uncertainty", fig)

    def noise(self):
        rows = self.data["noise_validation"]
        fig, axes = plt.subplots(1, 2, figsize=(12, 4.2))
        labels = [textwrap.fill(r["case"].replace("_", " "), 13) for r in rows]
        x = list(range(len(rows)))
        axes[0].bar(x, [r["ou_admissible"] / 10 for r in rows], color=self.teal)
        axes[0].set_ylabel("Numerically OU-admissible (%)")
        axes[0].set_ylim(0, 110)
        axes[0].set_title("1,000 synthetic point trials/case", fontsize=12)
        for delta, key, label, color in [
            (-0.18, "coverage_block_1", "Block 1", self.teal),
            (0.18, "coverage_block_5", "Block 5", self.blue),
        ]:
            axes[1].bar(
                [a + delta for a in x],
                [r[key] * 100 for r in rows],
                0.36,
                label=label,
                color=color,
            )
        axes[1].axhline(95, color=self.red, ls="--", lw=1, label="Nominal 95%")
        axes[1].set_ylabel("Latent-slope interval coverage (%)")
        axes[1].set_ylim(0, 110)
        axes[1].set_title("200 range trials/case", fontsize=12)
        axes[1].legend(fontsize=9)
        for ax in axes:
            ax.set_xticks(x, labels, fontsize=9)
        self.save("reversion_validation", fig)

    def lifecycle(self):
        rows = [
            r
            for r in self.data["execution"]["broker_frames"]
            if r["case"] == "partial_cancel"
        ]
        fig, axes = plt.subplots(2, 1, figsize=(11, 5), sharex=True)
        x = list(range(len(rows)))
        axes[0].step(
            x,
            [r["cash_usd"] for r in rows],
            where="post",
            color=self.blue,
            label="Cash",
        )
        axes[0].step(
            x,
            [r["reserved_cash_usd"] for r in rows],
            where="post",
            color=self.gold,
            label="Reserved cash",
        )
        axes[0].set_ylabel("USD")
        axes[0].legend(ncol=2, fontsize=10)
        axes[1].step(
            x,
            [r["position"] for r in rows],
            where="post",
            color=self.teal,
            label="Shares held",
        )
        axes[1].step(
            x,
            [r["open_orders"] for r in rows],
            where="post",
            color=self.red,
            label="Open orders",
        )
        axes[1].set_ylabel("Count")
        axes[1].legend(ncol=2, fontsize=10)
        axes[1].set_xticks(x, [str(i) for i in x])
        axes[1].set_xlabel("Saved broker/client checkpoint (event order)")
        self.save("broker_lifecycle", fig)

    def policy(self):
        rows = self.data["execution"]["policy_summaries"]
        fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
        labels = [r["policy"].replace("_", "\n") for r in rows]
        x = list(range(4))
        axes[0].bar(x, [r["fill_fraction"] * 100 for r in rows], color=self.teal)
        axes[0].set_ylabel("Filled/requested shares (%)")
        axes[0].set_ylim(0, 100)
        axes[0].set_title("36 matched workloads/policy", fontsize=12)
        bottom = [0.0] * 4
        for key, label, color in [
            ("spread_component_bps", "Spread", self.gold),
            ("unfilled_opportunity_bps", "Unfilled opportunity", self.blue),
        ]:
            vals = [r[key] for r in rows]
            axes[1].bar(x, vals, bottom=bottom, label=label, color=color)
            bottom = [a + b for a, b in zip(bottom, vals)]
        axes[1].bar(
            x,
            [r["timing_component_bps"] for r in rows],
            label="Timing",
            color=self.teal,
        )
        axes[1].scatter(
            x,
            [r["total_shortfall_bps"] for r in rows],
            color="#172f43",
            marker="D",
            s=28,
            label="Total",
        )
        axes[1].axhline(0, color="#445", lw=0.8)
        axes[1].set_ylabel("Shortfall (bp/requested notional)")
        axes[1].legend(fontsize=9, loc="upper left", bbox_to_anchor=(1, 1))
        for ax in axes:
            ax.set_xticks(x, labels, fontsize=10)
        self.save("execution_policies", fig)

    def quotes(self):
        c = self.data["execution"]["quote_example"]
        fig, ax = plt.subplots(figsize=(10, 3.5))
        q = c["quotes"]
        x = [r["at_seconds"] for r in q]
        ax.step(
            x,
            [r["bid_cents"] / 100 for r in q],
            where="post",
            label="Bid",
            color=self.blue,
        )
        ax.step(
            x,
            [r["ask_cents"] / 100 for r in q],
            where="post",
            label="Ask",
            color=self.teal,
        )
        ax.axhline(
            c["limit_cents"] / 100,
            color=self.gold,
            ls="--",
            label="Submitted buy limit",
        )
        fills = c["fills"]
        ax.scatter(
            [f["source_seconds"] for f in fills],
            [f["price_cents"] / 100 for f in fills],
            s=[25 * f["quantity"] for f in fills],
            color=self.red,
            zorder=4,
            label="Fills (area = shares)",
        )
        ax.set_xlabel("Synthetic source time (seconds)")
        ax.set_ylabel("Price (USD)")
        ax.legend(fontsize=10, ncol=2)
        self.save("quote_and_fills", fig)

    def processing(self):
        rows = self.runtime["windows"]
        modes = [
            "core",
            "copy_candidate",
            "journal_memory",
            "journal_sync",
            "paper_memory",
            "paper_sync",
        ]
        labels = [
            "Core",
            "Extra copy",
            "Typed memory",
            "Typed sync",
            "Paper memory",
            "Paper sync",
        ]
        palette = [self.teal, self.gold, self.blue, self.red, "#77539b", "#516c4d"]
        fig, axes = plt.subplots(1, 2, figsize=(12, 4.3))
        for mode, label, color in zip(modes, labels, palette):
            sizes = sorted({int(r["round_trips"]) for r in rows if r["mode"] == mode})
            groups = [
                [
                    float(r["p50_ns"]) / 1000
                    for r in rows
                    if r["mode"] == mode and int(r["round_trips"]) == n
                ]
                for n in sizes
            ]
            axes[0].plot(
                sizes,
                [statistics.median(g) for g in groups],
                marker="o",
                label=label,
                color=color,
            )
            axes[0].fill_between(
                sizes,
                [min(g) for g in groups],
                [max(g) for g in groups],
                alpha=0.10,
                color=color,
            )
        axes[0].set_yscale("log")
        axes[0].set_xticks([8, 32, 128])
        axes[0].set_xlabel("Retained round trips")
        axes[0].set_ylabel("Trial p50 input latency (us, log scale)")
        axes[0].legend(fontsize=8, ncol=2)
        rates = [
            statistics.median(
                float(r["operations_per_second"])
                for r in rows
                if r["mode"] == m and r["round_trips"] == "128"
            )
            for m in modes
        ]
        axes[1].bar(labels, rates, color=palette)
        axes[1].set_yscale("log")
        axes[1].tick_params(axis="x", labelrotation=30, labelsize=9)
        axes[1].set_ylabel("Timed input calls/second (log scale)")
        axes[1].set_title("128 round trips; median trial rate", fontsize=11)
        self.save("runtime_costs", fig)

    def recovery(self):
        fig, ax = plt.subplots(figsize=(10, 3.6))
        for platform, color in [("windows", self.teal), ("linux", self.blue)]:
            for mode, style, label in [
                ("journal_recovery", "--", "Typed"),
                ("paper_recovery", "-", "Paper"),
            ]:
                rows = [r for r in self.runtime[platform] if r["mode"] == mode]
                sizes = sorted({int(r["round_trips"]) for r in rows})
                groups = [
                    [
                        float(r["p50_ns"]) / 1e6
                        for r in rows
                        if int(r["round_trips"]) == n
                    ]
                    for n in sizes
                ]
                ax.plot(
                    sizes,
                    [statistics.median(g) for g in groups],
                    style,
                    marker="o",
                    color=color,
                    label=f"{platform.title()} / {label}",
                )
                ax.fill_between(
                    sizes,
                    [min(g) for g in groups],
                    [max(g) for g in groups],
                    color=color,
                    alpha=0.08,
                )
        ax.set_yscale("log")
        ax.set_xticks([8, 32, 128])
        ax.set_xlabel("Retained round trips")
        ax.set_ylabel("Warm recovery operation (ms, log scale)")
        ax.legend(ncol=2, fontsize=10)
        self.save("restart_recovery", fig)


class Publication:
    """One content model rendered as self-contained HTML and paginated PDF."""

    def __init__(self, root: Path, demo: Path):
        self.root = root
        self.demo = demo
        self.evidence = Evidence(root)
        self.content = json.loads((root / "docs/report_source.json").read_text())
        self.temporary = root / "scratch/finalization/publication"
        self.figures = Figures(self.evidence, self.temporary)
        self.math_count = 0
        self.styles = getSampleStyleSheet()
        self.styles.add(
            ParagraphStyle(
                "BodyReport",
                fontName="Helvetica",
                fontSize=9.5,
                leading=14,
                spaceAfter=8,
                textColor=colors.HexColor("#21394b"),
            )
        )
        self.styles.add(
            ParagraphStyle(
                "CaptionReport",
                fontName="Helvetica",
                fontSize=8,
                leading=11,
                spaceAfter=9,
                textColor=colors.HexColor("#4a6274"),
            )
        )
        self.styles.add(
            ParagraphStyle("TableReport", fontName="Helvetica", fontSize=8, leading=10)
        )
        self.styles.add(
            ParagraphStyle(
                "ChapterReport",
                fontName="Helvetica-Bold",
                fontSize=19,
                leading=24,
                spaceAfter=16,
                textColor=colors.HexColor("#17354d"),
            )
        )
        self.styles.add(
            ParagraphStyle(
                "CodeReport", fontName="Courier", fontSize=7, leading=10, spaceAfter=10
            )
        )

    @staticmethod
    def escaped(text):
        return html.escape(str(text), quote=True)

    def equation(self, latex):
        self.math_count += 1
        path = self.temporary / f"equation_{self.math_count}.png"
        fig = plt.figure(figsize=(10, 0.55))
        fig.text(0.5, 0.5, "$" + latex + "$", ha="center", va="center", fontsize=17)
        fig.savefig(
            path, dpi=190, bbox_inches="tight", pad_inches=0.10, facecolor="white"
        )
        plt.close(fig)
        return path

    def image(self, path, width=505, max_height=260):
        w, h = ImageReader(str(path)).getSize()
        scale = min(width / w, max_height / h)
        return Image(str(path), width=w * scale, height=h * scale)

    def table(self, columns, rows):
        p = lambda text: Paragraph(self.escaped(text), self.styles["TableReport"])
        values = [[p(c) for c in columns]] + [[p(c) for c in row] for row in rows]
        widths = [505 / len(columns)] * len(columns)
        t = Table(values, colWidths=widths, repeatRows=1, hAlign="LEFT")
        t.setStyle(
            TableStyle(
                [
                    ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#e7eff4")),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
                    ("TOPPADDING", (0, 0), (-1, -1), 7),
                    ("LINEBELOW", (0, 0), (-1, 0), 0.6, colors.HexColor("#1a6f80")),
                    ("LINEBELOW", (0, 1), (-1, -1), 0.25, colors.HexColor("#d4dfe6")),
                ]
            )
        )
        return t

    def page(self, canvas, document):
        stamp = "D:" + self.content["publication_date"].replace("-", "") + "000000Z"
        canvas.setDateFormatter(lambda *_: stamp)
        canvas.saveState()
        canvas.setStrokeColor(colors.HexColor("#d5e1e9"))
        canvas.line(45, 38, 550, 38)
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(colors.HexColor("#526a7a"))
        canvas.drawString(
            45, 24, "Algorithmic Trading Runtime | Research and execution study"
        )
        canvas.drawRightString(550, 24, str(document.page))
        canvas.restoreState()

    def build(self):
        self.figures.render()
        self.temporary.mkdir(parents=True, exist_ok=True)
        story = [
            Spacer(1, 35),
            Paragraph(self.escaped(self.content["title"]), self.styles["Title"]),
            Paragraph(self.escaped(self.content["subtitle"]), self.styles["Heading2"]),
            Spacer(1, 18),
            Paragraph("Kartik Vij | 25 October 2026", self.styles["BodyReport"]),
            Paragraph(
                self.escaped(self.content["abstract"]), self.styles["BodyReport"]
            ),
            Spacer(1, 18),
        ]
        parts = [
            '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">',
            "<title>Algorithmic Trading Runtime - Technical Report</title>",
            self.css(),
            '<header><p class="eyebrow">C++20 / Research / Execution / Recovery</p><h1>'
            + self.escaped(self.content["title"])
            + "</h1>",
            "<p>"
            + self.escaped(self.content["subtitle"])
            + "</p><p>Kartik Vij | 25 October 2026</p></header>",
            '<main><section class="abstract"><h2>Abstract</h2><p>'
            + self.escaped(self.content["abstract"])
            + "</p></section><nav><h2>Contents</h2><ol>",
        ]
        for chapter in self.content["chapters"]:
            parts.append(
                '<li><a href="#'
                + chapter["id"]
                + '">'
                + self.escaped(chapter["title"])
                + "</a></li>"
            )
            story.append(
                Paragraph(self.escaped(chapter["title"]), self.styles["BodyReport"])
            )
        parts.append("</ol></nav>")
        for chapter in self.content["chapters"]:
            story.extend(
                [
                    PageBreak(),
                    Paragraph(
                        self.escaped(chapter["title"]), self.styles["ChapterReport"]
                    ),
                ]
            )
            parts.append(
                '<section id="'
                + chapter["id"]
                + '"><h2>'
                + self.escaped(chapter["title"])
                + "</h2>"
            )
            for block in chapter["blocks"]:
                kind = block["type"]
                if kind == "paragraph":
                    story.append(
                        Paragraph(
                            self.escaped(block["text"]), self.styles["BodyReport"]
                        )
                    )
                    parts.append("<p>" + self.escaped(block["text"]) + "</p>")
                elif kind == "heading":
                    story.append(
                        Paragraph(self.escaped(block["text"]), self.styles["Heading3"])
                    )
                    parts.append("<h3>" + self.escaped(block["text"]) + "</h3>")
                elif kind == "equation":
                    path = self.equation(block["latex"])
                    story.append(self.image(path, max_height=40))
                    payload = base64.b64encode(path.read_bytes()).decode()
                    parts.append(
                        '<figure class="equation"><img alt="'
                        + self.escaped(block["latex"])
                        + '" src="data:image/png;base64,'
                        + payload
                        + '"></figure>'
                    )
                    if block.get("note"):
                        story.append(
                            Paragraph(
                                self.escaped(block["note"]),
                                self.styles["CaptionReport"],
                            )
                        )
                        parts.append(
                            '<p class="caption">' + self.escaped(block["note"]) + "</p>"
                        )
                elif kind == "figure":
                    svg, png = self.figures.paths[block["name"]]
                    story.append(
                        KeepTogether(
                            [
                                self.image(png),
                                Paragraph(
                                    self.escaped(block["caption"]),
                                    self.styles["CaptionReport"],
                                ),
                            ]
                        )
                    )
                    parts.append(
                        '<figure><img alt="'
                        + self.escaped(block["caption"])
                        + '" src="data:image/svg+xml;base64,'
                        + base64.b64encode(svg.read_bytes()).decode()
                        + '"><figcaption>'
                        + self.escaped(block["caption"])
                        + "</figcaption></figure>"
                    )
                elif kind == "table":
                    story.extend(
                        [self.table(block["columns"], block["rows"]), Spacer(1, 12)]
                    )
                    parts.append(
                        '<div class="scroll"><table><thead><tr>'
                        + "".join(
                            "<th>" + self.escaped(c) + "</th>" for c in block["columns"]
                        )
                        + "</tr></thead><tbody>"
                    )
                    parts.extend(
                        "<tr>"
                        + "".join("<td>" + self.escaped(c) + "</td>" for c in row)
                        + "</tr>"
                        for row in block["rows"]
                    )
                    parts.append("</tbody></table></div>")
                elif kind == "code":
                    story.append(Preformatted(block["text"], self.styles["CodeReport"]))
                    parts.append("<pre>" + self.escaped(block["text"]) + "</pre>")
                elif kind == "pagebreak":
                    story.append(PageBreak())
                elif kind == "reference":
                    label = self.escaped(block["text"])
                    url = self.escaped(block["url"])
                    story.append(
                        Paragraph(
                            label
                            + '<br/><link href="'
                            + url
                            + '" color="#137e87">'
                            + url
                            + "</link>",
                            self.styles["CaptionReport"],
                        )
                    )
                    parts.append(
                        '<p class="reference">'
                        + label
                        + '<br><a href="'
                        + url
                        + '">'
                        + url
                        + "</a></p>"
                    )
                else:
                    raise ValueError(f"Unsupported report block: {kind}")
            parts.append("</section>")
        parts.append(
            '<section id="replay"><h2>Interactive saved-event replay</h2><p>Offline C++ outputs. Event buttons select saved account/order states; no accounting runs in the viewer. The PDF contains the corresponding static figures and interpretation.</p>'
        )
        for label, sub in [
            ("Strategy decisions and execution", "strategies"),
            ("Broker lifecycle and recovery", "broker"),
        ]:
            view = (self.demo / sub / "report.html").read_text()
            view = re.sub(r'<a href="(?!#)[^"]+">([^<]+)</a>', r"<span>\1</span>", view)
            parts.append(
                "<details><summary>"
                + label
                + '</summary><p>Download links are omitted in this embedded view. Run project_demo for complete raw exports.</p><iframe title="'
                + label
                + '" srcdoc="'
                + self.escaped(view)
                + '"></iframe></details>'
            )
        parts.append(
            "</section></main><footer>Curated development summaries and synthetic diagnostics. Full data access and result interpretation are specified in the reproduction section.</footer></html>"
        )
        (self.root / "docs/report.html").write_text("".join(parts))
        rl_config.invariant = 1
        doc = SimpleDocTemplate(
            str(self.root / "docs/report.pdf"),
            pagesize=(595.276, 841.89),
            leftMargin=45,
            rightMargin=45,
            topMargin=43,
            bottomMargin=52,
            title=self.content["title"],
            author="Kartik Vij",
            pageCompression=1,
        )
        doc.build(story, onFirstPage=self.page, onLaterPages=self.page)
        files = [
            self.root / "research/build_report.py",
            self.root / "research/report_requirements.txt",
            self.root / "docs/report_source.json",
            self.root / "docs/evidence/manifest.json",
            self.root / "docs/report.html",
            self.root / "docs/report.pdf",
        ]
        files.extend(sorted((self.root / "docs/figures").glob("*.svg")))
        manifest = {
            "schema_version": 1,
            "renderer": "research/build_report.py",
            "demo_summary_sha256": hashlib.sha256(
                (self.demo / "summary.json").read_bytes()
            ).hexdigest(),
            "artifacts": [
                {
                    "path": str(path.relative_to(self.root)),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
                for path in files
            ],
        }
        (self.root / "docs/publication_manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n"
        )
        print("Published self-contained HTML, paginated PDF and eight evidence figures")

    @staticmethod
    def css():
        return """<style>html{scroll-behavior:smooth}body{font:17px/1.65 system-ui,sans-serif;margin:0;color:#20394b;background:#edf2f6}header{background:#17354d;color:white;padding:4rem max(5%,calc((100vw - 1100px)/2))}header h1{font-size:clamp(2rem,5vw,3.4rem);line-height:1.15;max-width:1000px}.eyebrow{letter-spacing:.12em;font-size:.8rem;text-transform:uppercase;color:#a5d8dd}main{max-width:1100px;margin:auto;padding:2rem 1.4rem}section,nav{background:white;padding:2rem;margin-bottom:1.5rem;border-radius:7px}h2,h3{line-height:1.3;color:#17354d}h2{font-size:1.6rem}a{color:#126f83}figure{margin:1.6rem 0}figure img{width:100%;height:auto}.equation{text-align:center}.equation img{max-width:850px;width:auto;max-height:80px;object-fit:contain}figcaption,.caption,.reference{font-size:.88rem;color:#506a7b}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.9rem}th,td{text-align:left;vertical-align:top;padding:.7rem;border-bottom:1px solid #dce5eb}th{background:#e7eff4}pre{overflow:auto;font-size:.82rem;background:#f0f5f8;padding:1rem}summary{cursor:pointer;font-weight:600;padding:1rem}iframe{width:100%;height:850px;border:1px solid #cbd9e3;background:white}footer{max-width:1100px;padding:2rem;margin:auto;font-size:.85rem;color:#536c7d}@media(max-width:600px){section,nav{padding:1rem}.equation img{width:100%}}</style>"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--demo",
        type=Path,
        required=True,
        help="Verified project_demo output directory",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    Publication(root, args.demo.resolve()).build()


if __name__ == "__main__":
    main()
