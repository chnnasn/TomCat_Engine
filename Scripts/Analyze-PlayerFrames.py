"""Rank Player CPU frames and compare matching frame IDs from a fixed-input replay."""

import argparse
import json
from collections import defaultdict
from pathlib import Path


# Pick the deepest measured stage at each point on the main thread so a text
# layout nested inside rendering contributes only once to the frame total.
STAGES = {
    "Managed Update", "Managed FixedUpdate", "Physics Box2D Step", "Runtime UI Update",
    "Text layout", "Texture upload queue", "Scene Runtime Render", "Audio Update",
    "Present / VSync wait",
}


def read_frames(path):
    frames = {}
    samples = []
    # FrameProfiler writes the samples of each retained frame directly before
    # its summary, including worker samples that finish late in the frame.
    for event in json.loads(path.read_text(encoding="utf-8"))["traceEvents"]:
        if event.get("cat") == "CPU":
            samples.append(event)
            continue
        if event.get("name") != "Frame":
            continue
        main = next((s["tid"] for s in samples if s["name"] == "RunLoop"), None)
        start, end = event["ts"], event["ts"] + event["dur"]
        edges = defaultdict(list)
        for index, sample in enumerate(samples):
            if sample["tid"] != main or sample["name"] not in STAGES:
                continue
            a = max(start, sample["ts"])
            b = min(end, sample["ts"] + sample["dur"])
            if b > a:
                edges[a].append((index, True))
                edges[b].append((index, False))
        edges.setdefault(end, [])
        totals = defaultdict(float)
        active = set()
        previous = start
        for timestamp in sorted(edges):
            if active:
                inner = min(active, key=lambda i: (samples[i]["dur"], i))
                totals[samples[inner]["name"]] += timestamp - previous
            else:
                totals["Other / unmeasured"] += timestamp - previous
            for index, entering in edges[timestamp]:
                if entering:
                    active.add(index)
                else:
                    active.discard(index)
            previous = timestamp
        frames[event["args"]["frame"]] = (event["dur"], dict(totals), event["args"])
        samples.clear()
    if not frames:
        raise ValueError(f"No completed Player frames in {path}")
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--top", type=int, default=5)
    args = parser.parse_args()
    if args.top < 1:
        parser.error("--top must be positive")
    frames = read_frames(args.trace)
    baseline = read_frames(args.baseline) if args.baseline else {}
    for frame in sorted(frames, key=lambda f: -frames[f][0])[:args.top]:
        duration, stages, stats = frames[frame]
        parts = [f"frame {frame}", f"CPU {duration / 1000:.2f} ms"]
        if frame in baseline:
            parts.append(f"change {(duration - baseline[frame][0]) / 1000:+.2f} ms")
        elif args.baseline:
            parts.append("baseline frame unavailable")
        if stats["gpuMs"] >= 0:
            parts.append(f"GPU {stats['gpuMs']:.2f} ms")
        for name, time in sorted(stages.items(), key=lambda item: -item[1])[:3]:
            parts.append(f"{name} {time / 1000:.2f} ms")
        if stats["droppedScopes"]:
            parts.append(f"{stats['droppedScopes']} dropped scopes")
        print(" | ".join(parts))


if __name__ == "__main__":
    main()
