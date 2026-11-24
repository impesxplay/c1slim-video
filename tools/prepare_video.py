"""Prepare a lossless grayscale source for the C1-Slim video picker."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path, help="new .mkv file")
parser.add_argument("--ffmpeg", default="ffmpeg")
args = parser.parse_args()
if args.output.suffix.lower() != ".mkv":
    parser.error("output must end in .mkv")
filters = (
    "transpose=dir=clock:passthrough=landscape,"
    "scale=296:152:force_original_aspect_ratio=decrease:flags=area,"
    "pad=296:152:(ow-iw)/2:(oh-ih)/2:color=white,setsar=1,fps=30,format=gray"
)
subprocess.run([
    args.ffmpeg, "-nostdin", "-hide_banner", "-n", "-i", str(args.source),
    "-map", "0:v:0", "-map", "0:a:0?", "-vf", filters,
    "-c:v", "ffv1", "-level", "1", "-pix_fmt", "gray",
    "-c:a", "pcm_s16le", "-ac", "1", "-ar", "48000", str(args.output),
], check=True)
