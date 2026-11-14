"""Rotate the raw KiCad positions into the JLCPCB CPL and write a review list of the corrections.

Rules: package offsets confirmed in the universal-weather-node JLCPCB previews (2026-11-03 .. 2026-11-06)
and in this board's own previews (2026-11-07 07:23:22 UTC, 2026-11-08 22:10:37 UTC). Articles or packages that were not part of
those previews are marked NEEDS_PREVIEW: check them in the order preview before paying."""

import argparse
import csv
import math
from pathlib import Path

# footprint name -> (offset, LCSC articles confirmed in a weather-node preview)
RULES = {
    "SOT-23": (180, {"C15127", "C20917", "C49338", "C7420333", "C20526", "C37130", "C10493", "C8545", "C5446",
                     "C150242", "C82471", "C68978"}),
    "SOT-23-5": (270, set()),                       # AP2112K (C51118) on the weather node; C7835 see ARTICLE_OFFSET
    "SOT-23-6": (270, {"C12401", "C84817"}),
    "SOT-353_SC-70-5": (270, {"C350557"}),
    "SOIC-8_3.9x4.9mm_P1.27mm": (270, {"C190783"}),
    "Texas_HTSOP-8-1EP_3.9x4.9mm_P1.27mm_EP2.95x4.9mm_Mask2.4x3.1mm_ThermalVias": (270, {"C1858393"}),
    "TSSOP-10_3x3mm_P0.5mm": (270, {"C37593"}),
    "Vishay_PowerPAK_1212-8_Single": (270, {"C431191"}),
    "TO-252-2": (0, {"C130104"}),
    "SOT-89-3": (180, {"C347258"}),
    "SMDIP-4_W9.53mm": (0, {"C3025164"}),
    # TPS16630 (C1849461): the 2026-11-08 preview showed the model 90 degrees off with +270 (leads top/bottom, pin 1 top right)
    "HTSSOP-20-1EP_4.4x6.5mm_P0.65mm_EP3.4x6.5mm_Mask2.96x2.96mm_ThermalVias": (0, set()),
}
# article-specific offsets that differ from the package rule
# C7835 (TI SN74LVC1G14DBVR): the 2026-11-07 preview showed the model 90 degrees off with +270 (three leads top)
# C14170 (ST L78L33ACUTR, SOT-89): the 2026-11-08 preview showed the model 180 degrees off with the UMW 78L05 rule (tab at the bottom)
ARTICLE_OFFSET = {"C7835": 180, "C14170": 0}
# JLC's TO-252 model sits 1.4 mm towards the tab compared to the KiCad origin (leads only touch the pad edge,
# metal tab beyond the tab pad, weather-node and 2026-11-07 previews): move the centroid towards the leads
LEAD_SHIFT = {"C130104": 1.4}
POLAR = ("D_SMA", "D_SMB", "D_SMC", "D_SOD-123", "CP_Elec")
CPL_FIELDS = ["Designator", "Mid X", "Mid Y", "Layer", "Rotation"]
REVIEW_FIELDS = ["Designator", "LCSC Part #", "Footprint", "KiCad Rotation", "Offset", "CPL Rotation", "Status"]


def build(raw_rows, bom_rows):
    parts = {}
    for row in bom_rows:
        for ref in row["Designator"].split(","):
            parts[ref.strip()] = row
    placements, review = [], []
    for row in raw_rows:
        ref = row["Designator"]
        if row["Layer"] != "Top":
            raise ValueError(f"{ref}: only top-side assembly is supported")
        part = parts[ref]
        fp = part["Footprint"].split(":")[-1]
        offset, status = 0, "NONPOLAR_CHECK"
        if fp in RULES:
            offset, confirmed = RULES[fp]
            status = "PREVIEW_MATCHED" if part["LCSC Part #"] in confirmed else "NEEDS_PREVIEW"
        elif fp.startswith(POLAR):
            status = "POLARITY_CHECK"           # cathode band / plus at KiCad pad 1, as on the weather node
        if part["LCSC Part #"] in ARTICLE_OFFSET:
            offset, status = ARTICLE_OFFSET[part["LCSC Part #"]], "PREVIEW_MATCHED"
        angle = float(row["Rotation"])
        rotation = (angle + offset) % 360
        placement = dict(row)
        placement["Rotation"] = f"{rotation:.6f}"
        if part["LCSC Part #"] in LEAD_SHIFT:
            # KiCad TO-252 at 0 degrees has its leads on the left (-X); the CPL Y axis points up
            d, a = LEAD_SHIFT[part["LCSC Part #"]], math.radians(angle)
            x, y = (float(row[k].removesuffix("mm")) for k in ("Mid X", "Mid Y"))
            placement["Mid X"] = f"{x - d * math.cos(a):.6f}mm"
            placement["Mid Y"] = f"{y - d * math.sin(a):.6f}mm"
            status += f"_SHIFT_{d}MM_TO_LEADS"
        placements.append(placement)
        review.append(dict(zip(REVIEW_FIELDS, [ref, part["LCSC Part #"], fp, row["Rotation"], offset,
                                               placement["Rotation"], status])))
    return placements, review


def write_csv(path, fields, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw_cpl", type=Path)
    parser.add_argument("bom", type=Path)
    parser.add_argument("cpl", type=Path)
    parser.add_argument("review", type=Path)
    args = parser.parse_args()
    with args.raw_cpl.open(newline="") as stream:
        raw = list(csv.DictReader(stream))
    with args.bom.open(newline="") as stream:
        bom = list(csv.DictReader(stream))
    placements, review = build(raw, bom)
    write_csv(args.cpl, CPL_FIELDS, placements)
    write_csv(args.review, REVIEW_FIELDS, review)
    pending = sorted(r["Designator"] for r in review if r["Status"] == "NEEDS_PREVIEW")
    print(f"CPL: {len(placements)} placements, rotated by package rule; check in the JLCPCB preview: {', '.join(pending) or 'none'}")


if __name__ == "__main__":
    main()
