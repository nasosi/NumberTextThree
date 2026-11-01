"""Draws the benchmark summary as SVG, in a light and a dark version, from the CSV that `Bench --csv` prints.

For example,

    python Benchmarks/Plot.py results.csv "Intel Core i5-13600K, Windows 11, MSVC 19.51" Results/i5-13600K-msvc

writes Results/i5-13600K-msvc-light.svg and Results/i5-13600K-msvc-dark.svg. Each one has the machine as its first and
largest heading, and below that four panels for reading and writing doubles and floats on one dataset, which is
canada.json unless --dataset names another. A panel shows each library's minimum nanoseconds per number, fastest first,
with NumberTextThree in color and the rest in gray. The C library's strtod, strtof and printf are left out of the plot
because they are an order of magnitude slower, but the tables still have them. The script uses nothing beyond the
standard library.
"""

import argparse
import csv
from xml.sax.saxutils import escape

THEMES = {
    "light": {"text": "#1f2328", "muted": "#59636e", "grid": "#d1d9e0", "accent": "#2a78d6", "context": "#8c959f"},
    "dark": {"text": "#e6edf3", "muted": "#9198a1", "grid": "#3d444d", "accent": "#3987e5", "context": "#6e7681"},
}

PANELS = [
    ("read-f64", "Read double"),
    ("write-f64", "Write double"),
    ("read-f32", "Read float"),
    ("write-f32", "Write float"),
]

LEFT_OUT = {"strtod", "strtof", "printf"}
HIGHLIGHT = "ntt"

WIDTH = 760
PANEL_WIDTH = 350
COLUMN_GAP = 60
MARGIN = 0
NAME_WIDTH = 78
VALUE_WIDTH = 40
BAR_HEIGHT = 14
BAR_GAP = 8
PANEL_TITLE_HEIGHT = 28
PANEL_GAP = 28
FONT = "system-ui, -apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"


def read_results(path, dataset):
    times = {}
    for row in csv.DictReader(open(path, newline="")):
        if row["dataset"] == dataset and row["library"] not in LEFT_OUT:
            times.setdefault(row["operation"], {})[row["library"]] = float(row["min_ns"])
    return times


def nice_step(largest):
    for step in (1, 2, 5, 10, 20, 25, 50, 100, 200, 500):
        if largest / step <= 5:
            return step
    return 1000


def bar_path(x, y, width, height, radius):
    """Returns the SVG path of a bar that is anchored at x and has rounded corners at its far end."""
    radius = min(radius, width, height / 2)
    return (
        f"M{x:.1f},{y:.1f}h{width - radius:.1f}a{radius},{radius} 0 0 1 {radius},{radius}"
        f"v{height - 2 * radius:.1f}a{radius},{radius} 0 0 1 {-radius},{radius}h{-(width - radius):.1f}z"
    )


def panel(out, theme, x0, y0, title, times, scale_max):
    colors = THEMES[theme]
    plot_x = x0 + NAME_WIDTH
    plot_width = PANEL_WIDTH - NAME_WIDTH - VALUE_WIDTH
    rows = sorted(times.items(), key=lambda item: item[1])
    plot_height = len(rows) * (BAR_HEIGHT + BAR_GAP) - BAR_GAP

    out.append(f'<text x="{x0}" y="{y0 + 16}" font-size="14" font-weight="600" fill="{colors["text"]}">{escape(title)}</text>')
    top = y0 + PANEL_TITLE_HEIGHT

    step = nice_step(scale_max)
    tick = 0
    while tick <= scale_max:
        x = plot_x + plot_width * tick / scale_max
        out.append(f'<line x1="{x:.1f}" y1="{top - 4}" x2="{x:.1f}" y2="{top + plot_height + 4}" stroke="{colors["grid"]}" stroke-width="1"/>')
        out.append(f'<text x="{x:.1f}" y="{top + plot_height + 18}" font-size="11" text-anchor="middle" fill="{colors["muted"]}">{tick}</text>')
        tick += step

    for index, (library, value) in enumerate(rows):
        y = top + index * (BAR_HEIGHT + BAR_GAP)
        highlighted = library == HIGHLIGHT
        fill = colors["accent"] if highlighted else colors["context"]
        weight = ' font-weight="600"' if highlighted else ""
        width = max(plot_width * value / scale_max, 1)
        out.append(f'<text x="{plot_x - 8}" y="{y + BAR_HEIGHT - 3}" font-size="12" text-anchor="end" fill="{colors["text"]}"{weight}>{escape(library)}</text>')
        out.append(f'<path d="{bar_path(plot_x, y, width, BAR_HEIGHT, 4)}" fill="{fill}"/>')
        out.append(f'<text x="{plot_x + width + 6:.1f}" y="{y + BAR_HEIGHT - 3}" font-size="12" fill="{colors["text"]}"{weight}>{value:.1f}</text>')

    return PANEL_TITLE_HEIGHT + plot_height + 24


def draw(theme, times, machine, dataset):
    colors = THEMES[theme]
    out = []
    y = 0
    out.append(f'<text x="{MARGIN}" y="{y + 22}" font-size="20" font-weight="700" fill="{colors["text"]}">{escape(machine)}</text>')
    out.append(f'<text x="{MARGIN}" y="{y + 46}" font-size="14" fill="{colors["text"]}">Nanoseconds per number on {escape(dataset)}, lower is better</text>')
    out.append(f'<text x="{MARGIN}" y="{y + 64}" font-size="12" fill="{colors["muted"]}">Minimum of 5 pinned runs.</text>')
    y += 86

    for row in range(2):
        heights = []
        for column in range(2):
            operation, title = PANELS[row * 2 + column]
            if operation not in times:
                continue
            panel_times = times[operation]
            x0 = MARGIN + column * (PANEL_WIDTH + COLUMN_GAP)
            heights.append(panel(out, theme, x0, y, title, panel_times, max(panel_times.values()) * 1.05))
        y += max(heights, default=0) + PANEL_GAP

    height = y - PANEL_GAP + 4
    header = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" viewBox="0 0 {WIDTH} {height}" '
        f'font-family="{FONT}" role="img">'
        f"<title>{escape(machine)}: nanoseconds per number on {escape(dataset)}</title>"
    )
    return header + "".join(out) + "</svg>\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv")
    parser.add_argument("machine", help="CPU, OS and compiler, which the plot shows as its first and largest heading")
    parser.add_argument("prefix", help="output path without the -light.svg / -dark.svg ending")
    parser.add_argument("--dataset", default="canada.json")
    arguments = parser.parse_args()

    times = read_results(arguments.csv, arguments.dataset)
    for theme in THEMES:
        with open(f"{arguments.prefix}-{theme}.svg", "w", encoding="utf-8", newline="\n") as file:
            file.write(draw(theme, times, arguments.machine, arguments.dataset))


if __name__ == "__main__":
    main()
