"""
weather-room — LIVE DASHBOARD v2.8
Real-time air quality monitor
Reads from ESP32 over Serial, plots live scrolling data

THERMAL NOTE:
  AHT21 sits on the ENS160 board. ENS160 self-heating causes AHT21
  to read ~2-3 C above actual room temperature. This is a placement
  issue not a sensor defect. AHT20 and BMP280 agree within 0.5 C
  and are more representative of actual room temperature.
  All values are plotted raw with no correction applied.

AQI NOTE:
  ENS160 reports AQI-UBA (German Federal Environmental Agency standard).
  Range: 1-5
    1 — Excellent  (no measures needed)
    2 — Good       (no irritation or impact on well-being)
    3 — Moderate   (sensitive people may experience slight irritation)
    4 — Poor       (irritation and discomfort, take action)
    5 — Unhealthy  (heavy irritation, avoid exposure)
"""

import serial
import matplotlib
matplotlib.use("MacOSX")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import matplotlib.ticker as ticker
from matplotlib.animation import FuncAnimation
from matplotlib import rcParams
from collections import deque
import threading
import time
import re
from datetime import datetime

# =============================================================
# CONFIGURATION
# =============================================================
SERIAL_PORT = "/dev/cu.usbserial-1430"
BAUD_RATE   = 115200
MAX_POINTS  = 120
UPDATE_MS   = 505

# Use the system local timezone automatically 
LOCAL_TZ = datetime.now().astimezone().tzinfo

# =============================================================
# THEME
# =============================================================
BG_DARK    = "#04090d"
BG_PANEL   = "#071320"
BG_FOOTER  = "#080f18"
GRID_C     = "#0a1e2e"
SPINE_C    = "#0d2840"
TICK_C     = "#1e4060"
LABEL_C    = "#2a5a7a"

C_AHT21_T  = "#00e5ff"
C_AHT20_T  = "#0066ff"
C_BMP_T    = "#b36bff"
C_AHT21_H  = "#00ffaa"
C_AHT20_H  = "#00ff00"
C_PRESS    = "#ff9900"
C_CO2      = "#ff3a3a"
C_TVOC     = "#ff6600"
C_PM1      = "#c46bff"
C_PM25     = "#e357ff"
C_PM10     = "#ff67c8"

AQI_COLORS = {
    1: "#00e676",
    2: "#76ff03",
    3: "#ffea00",
    4: "#ff6d00",
    5: "#ff1744",
}
AQI_LABELS = {
    1: "EXCELLENT",
    2: "GOOD",
    3: "MODERATE",
    4: "POOR",
    5: "UNHEALTHY",
}
AQI_BAND_COLORS = {
    1: "#00e67610",
    2: "#76ff0310",
    3: "#ffea0010",
    4: "#ff6d0015",
    5: "#ff174415",
}

# =============================================================
# DATA BUFFERS
# =============================================================
KEYS = ["temp_aht21","temp_aht20","temp_bmp",
        "hum_aht21","hum_aht20",
        "pressure","altitude",
        "co2","tvoc","aqi",
        "pm1","pm25","pm10"]

data   = {k: deque([float("nan")] * MAX_POINTS, maxlen=MAX_POINTS) for k in KEYS}
timestamps = deque([None] * MAX_POINTS, maxlen=MAX_POINTS)
latest = {k: None for k in KEYS}
lock   = threading.Lock()
status_msg = ["Connecting..."]

# =============================================================
# SERIAL READER
# =============================================================
def parse_val(line, tag):
    if tag in line:
        m = re.search(r":\s*([\-\d\.]+)", line)
        if m:
            try: return float(m.group(1))
            except: pass
    return None

def serial_reader():
    ser = None
    while True:
        try:
            if ser is None or not ser.is_open:
                ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
                status_msg[0] = f"Connected  ·  {SERIAL_PORT}  ·  {BAUD_RATE} baud"
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line: continue
            with lock:
                current_time = datetime.now(LOCAL_TZ)
                data_pushed = False
                
                def push(key, val, lo=-999, hi=999):
                    nonlocal data_pushed
                    if val is not None and lo < val < hi:
                        data[key].append(val); latest[key] = val
                        data_pushed = True

                push("temp_aht21", parse_val(line, "[AHT21]  Temperature"), -40, 85)
                push("temp_aht20", parse_val(line, "[AHT20]  Temperature"), -40, 85)
                push("temp_bmp",   parse_val(line, "[BMP280] Temperature"), -40, 85)
                push("hum_aht21",  parse_val(line, "[AHT21]  Humidity"),    0, 100)
                push("hum_aht20",  parse_val(line, "[AHT20]  Humidity"),    0, 100)
                push("pressure",   parse_val(line, "[BMP280] Pressure"),    800, 1200)
                push("altitude",   parse_val(line, "[BMP280] Altitude"),    -500, 9000)
                push("co2",        parse_val(line, "[ENS160] CO2"),         0, 60000)
                push("tvoc",       parse_val(line, "[ENS160] TVOC"),        0, 60000)
                push("aqi",        parse_val(line, "[ENS160] AQI"),         0, 6)
                push("pm1",        parse_val(line, "[PMS]    PM1.0"),       0, 1000)
                push("pm25",       parse_val(line, "[PMS]    PM2.5"),       0, 1000)
                push("pm10",       parse_val(line, "[PMS]    PM10"),        0, 1000)
                
                # Add timestamp for this data point
                if data_pushed:
                    timestamps.append(current_time)

        except serial.SerialException:
            status_msg[0] = "Disconnected — retrying..."
            if ser:
                try: ser.close()
                except: pass
            ser = None
            time.sleep(3)
        except Exception:
            time.sleep(0.1)

# =============================================================
# HELPERS
# =============================================================
def style(ax, title, unit, color):
    ax.set_facecolor(BG_PANEL)
    ax.set_title(title, color=color, fontsize=9, fontweight="bold",
                 loc="left", pad=4, fontfamily="monospace")
    ax.set_ylabel(unit, color=LABEL_C, fontsize=7.5,
                  fontfamily="monospace", labelpad=4)
    ax.tick_params(axis="both", colors=TICK_C, labelsize=7, length=3, pad=3)
    for lbl in ax.get_xticklabels() + ax.get_yticklabels():
        lbl.set_fontfamily("monospace"); lbl.set_color(TICK_C)
    for spine in ax.spines.values():
        spine.set_edgecolor(SPINE_C); spine.set_linewidth(0.6)
    ax.grid(True, color=GRID_C, linewidth=0.5, linestyle="-", alpha=0.8)
    ax.set_xlim(0, MAX_POINTS)
    format_time_axis(ax)
    ax.yaxis.set_major_locator(ticker.MaxNLocator(4, prune="both"))

def mkline(ax, color, lw=1.5, ls="-", alpha=1.0):
    return ax.plot([], [], color=color, lw=lw, ls=ls,
                   alpha=alpha, solid_capstyle="round")[0]

def setdata(line, buf):
    y = list(buf)
    line.set_data(range(len(y)), y)

def autoscale(ax, *bufs, pad=0.08):
    vals = [v for b in bufs for v in b if v == v]
    if len(vals) < 2: return
    lo, hi = min(vals), max(vals)
    rng = hi - lo if hi != lo else max(abs(hi) * 0.1, 0.5)
    ax.set_ylim(lo - rng * pad, hi + rng * pad)

def mklegend(ax, lines, labels, colors):
    leg = ax.legend(lines, labels, fontsize=6.5,
                    facecolor=BG_PANEL, edgecolor=SPINE_C,
                    framealpha=0.9, loc="upper left",
                    handlelength=1.8, handletextpad=0.5,
                    borderpad=0.5, labelspacing=0.3)
    for text, color in zip(leg.get_texts(), colors):
        text.set_color(color); text.set_fontfamily("monospace")

def aqi_color(val):
    if val is None or val != val: return "#334455"
    return AQI_COLORS.get(int(round(val)), "#334455")

def format_time_axis(ax):
    """Format x-axis to show real time labels in the local system timezone."""
    def tick_formatter(x, pos):
        idx = int(x)
        if idx < 0 or idx >= len(timestamps):
            return ""
        ts = timestamps[idx]
        if ts is None:
            return ""
        return ts.strftime("%H:%M")
    
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(tick_formatter))
    ax.xaxis.set_major_locator(ticker.MultipleLocator(30))

def draw_aqi_segments(ax, aqi_buf, seg_lines):
    for ln in seg_lines:
        ln.set_data([], [])
    y = list(aqi_buf)
    x = list(range(len(y)))
    segments = []
    i = 0
    while i < len(y):
        if y[i] != y[i]:
            i += 1
            continue
        val = int(round(y[i]))
        j = i
        while j < len(y) and y[j] == y[j] and int(round(y[j])) == val:
            j += 1
        end = min(j, len(y) - 1)
        segments.append((x[i:end+1], y[i:end+1], val))
        i = j
    for idx, (sx, sy, val) in enumerate(segments):
        if idx < len(seg_lines):
            seg_lines[idx].set_data(sx, sy)
            seg_lines[idx].set_color(AQI_COLORS.get(val, "#334455"))
    return seg_lines

# =============================================================
# FIGURE
# =============================================================
rcParams.update({
    "font.family": "monospace",
    "font.size": 8,
    "figure.dpi": 120,
})

fig = plt.figure(figsize=(18, 10), facecolor=BG_DARK)
fig.canvas.manager.set_window_title("weather-room — live dashboard")

# =============================================================
# LAYOUT CONSTANTS — tweak to taste
# =============================================================
TITLE_Y     = 0.970   # main title y (figure fraction)
STATUS_Y    = 0.945   # status line y
GRID_TOP    = 0.870   # top of plot grid — dropped further for breathing room
GRID_BOTTOM = 0.220   # bottom of grid — pushed much lower to give particulates room
GRID_LEFT   = 0.070   # left margin — pulled inward for centering
GRID_RIGHT  = 0.960
HSPACE      = 0.32    # vertical gap between rows (smaller = more compact)
WSPACE      = 0.28    # horizontal gap between columns

# Title and status — top band with clear separation from first row
fig.text(0.5, TITLE_Y,
         "WEATHER - ROOM //  LIVE  DASHBOARD",
         ha="center", va="top", color=C_AHT21_T,
         fontsize=11, fontfamily="monospace", fontweight="bold")

status_txt = fig.text(0.5, STATUS_Y, status_msg[0],
                      ha="center", va="top", color=LABEL_C,
                      fontsize=7.5, fontfamily="monospace")

# Date display — top right
date_txt = fig.text(0.94, TITLE_Y, "",
                    ha="right", va="top", color=C_AHT21_T,
                    fontsize=9.5, fontfamily="monospace", fontweight="bold")

# Compact reference card — quick interpretation guide
ref_text = (
    "REFERENCE VALUES (GOOD):\n"
    "--------------------------\n"
    "RH 40-60% | CO2 <800 ppm | TVOC <150 ppb | AQI 1-2\n"
    # "TVOC <150 ppb | AQI 1-2\n"
    "PM2.5 <12 | PM10 <20 ug/m3"
)
fig.text(0.08, 0.975, ref_text,
         ha="left", va="top", color=LABEL_C,
         fontsize=7.5, fontfamily="monospace", linespacing=1.25,
         bbox=dict(facecolor=BG_FOOTER, edgecolor=SPINE_C,
                   linewidth=0.8, alpha=0.80, boxstyle="round,pad=0.45"))

# Plot grid — 3 rows of plots only. Footer is a separate axes placed manually
# so nothing from the grid can render a line across the particulates plot.
gs = gridspec.GridSpec(
    3, 3, figure=fig,
    left=GRID_LEFT, right=GRID_RIGHT,
    top=GRID_TOP, bottom=GRID_BOTTOM,
    hspace=HSPACE, wspace=WSPACE,
    height_ratios=[1, 1, 1.2]
)

ax_temp  = fig.add_subplot(gs[0, 0])
ax_hum   = fig.add_subplot(gs[0, 1])
ax_press = fig.add_subplot(gs[0, 2])
ax_co2   = fig.add_subplot(gs[1, 0])
ax_tvoc  = fig.add_subplot(gs[1, 1])
ax_aqi   = fig.add_subplot(gs[1, 2])
ax_pm    = fig.add_subplot(gs[2, :])

# Footer — placed manually at the bottom, fully detached from the grid
FOOTER_H       = 0.125   # taller footer to avoid crowding at lower edge
FOOTER_MARGIN  = 0.030   # gap between grid bottom and footer
footer_bottom  = GRID_BOTTOM - FOOTER_MARGIN - FOOTER_H
ax_foot = fig.add_axes([GRID_LEFT, footer_bottom,
                        GRID_RIGHT - GRID_LEFT, FOOTER_H])

style(ax_temp,  "TEMPERATURE",   "°C",    C_AHT21_T)
style(ax_hum,   "HUMIDITY",      "%",     C_AHT21_H)
style(ax_press, "PRESSURE",      "hPa",   C_PRESS)
style(ax_co2,   "CO₂",           "ppm",   C_CO2)
style(ax_tvoc,  "TVOC",          "ppb",   C_TVOC)
style(ax_pm,    "PARTICULATES",  "µg/m³", C_PM25)

# AQI axes
ax_aqi.set_facecolor(BG_PANEL)
ax_aqi.set_title("AQI  (UBA 1–5)", color="#00e676", fontsize=9,
                 fontweight="bold", loc="left", pad=4, fontfamily="monospace")
ax_aqi.tick_params(axis="both", colors=TICK_C, labelsize=7, length=3, pad=3)
for lbl in ax_aqi.get_xticklabels() + ax_aqi.get_yticklabels():
    lbl.set_fontfamily("monospace"); lbl.set_color(TICK_C)
for spine in ax_aqi.spines.values():
    spine.set_edgecolor(SPINE_C); spine.set_linewidth(0.6)
ax_aqi.grid(True, color=GRID_C, linewidth=0.5, linestyle="-", alpha=0.8)
ax_aqi.set_xlim(0, MAX_POINTS)
ax_aqi.set_ylim(0.5, 5.5)
format_time_axis(ax_aqi)
ax_aqi.yaxis.set_major_locator(ticker.FixedLocator([1, 2, 3, 4, 5]))

# Colored bands
for level, bc in AQI_BAND_COLORS.items():
    ax_aqi.axhspan(level - 0.5, level + 0.5, color=bc, zorder=0)

# Labels — in upper third of each band, away from the line center
for level in range(1, 6):
    ax_aqi.text(3, level + 0.32,
                f"{level}  {AQI_LABELS[level]}",
                color=AQI_COLORS[level], fontsize=5.5,
                fontfamily="monospace", va="center", ha="left", alpha=0.8)

# Footer styling — no spines at all so nothing draws a line near the plot
ax_foot.set_facecolor(BG_FOOTER)
ax_foot.set_xticks([]); ax_foot.set_yticks([])
for spine in ax_foot.spines.values():
    spine.set_visible(False)

# Lines
l_t21  = mkline(ax_temp,  C_AHT21_T, lw=1.5)
l_t20  = mkline(ax_temp,  C_AHT20_T, lw=1.5)
l_tbmp = mkline(ax_temp,  C_BMP_T,   lw=1.5)
l_h21  = mkline(ax_hum,   C_AHT21_H, lw=1.5)
l_h20  = mkline(ax_hum,   C_AHT20_H, lw=1.5)
l_p    = mkline(ax_press, C_PRESS,   lw=1.6)
l_co2  = mkline(ax_co2,   C_CO2,     lw=1.6)
l_tvoc = mkline(ax_tvoc,  C_TVOC,    lw=1.6)
l_pm1  = mkline(ax_pm,    C_PM1,     lw=1.2, ls=":",  alpha=1.0)
l_pm25 = mkline(ax_pm,    C_PM25,    lw=1.6)
l_pm10 = mkline(ax_pm,    C_PM10,    lw=1.2, ls="--", alpha=0.85)

MAX_AQI_SEGMENTS = MAX_POINTS // 2
aqi_seg_lines = [
    ax_aqi.plot([], [], lw=2.0, solid_capstyle="round", zorder=5)[0]
    for _ in range(MAX_AQI_SEGMENTS)
]

mklegend(ax_temp,
         [l_t21, l_t20, l_tbmp],
         ["AHT21 (ENS160 board)", "AHT20 (BMP280 board)", "BMP280"],
         [C_AHT21_T, C_AHT20_T, C_BMP_T])

mklegend(ax_hum,
         [l_h21, l_h20],
         ["AHT21 (ENS160 board)", "AHT20 (BMP280 board)"],
         [C_AHT21_H, C_AHT20_H])

mklegend(ax_pm,
         [l_pm1, l_pm25, l_pm10],
         ["PM1.0", "PM2.5", "PM10"],
         [C_PM1, C_PM25, C_PM10])

FOOTER_ITEMS = [
    ("AHT21 TEMP",  "temp_aht21", "°C",    C_AHT21_T),
    ("AHT20 TEMP",  "temp_aht20", "°C",    C_AHT20_T),
    ("BMP280 TEMP", "temp_bmp",   "°C",    C_BMP_T),
    ("AHT21 HUM",   "hum_aht21",  "%",     C_AHT21_H),
    ("AHT20 HUM",   "hum_aht20",  "%",     C_AHT20_H),
    ("PRESSURE",    "pressure",   "hPa",   C_PRESS),
    ("CO₂",         "co2",        "ppm",   C_CO2),
    ("TVOC",        "tvoc",       "ppb",   C_TVOC),
    ("AQI",         "aqi",        "",      "#00e676"),
    ("PM1.0",       "pm1",        "µg/m³", C_PM1),
    ("PM2.5",       "pm25",       "µg/m³", C_PM25),
    ("PM10",        "pm10",       "µg/m³", C_PM10),
]

FOOTER_COLS = 6
FOOTER_ROWS = 2
CELL_W = 1.0 / FOOTER_COLS
val_texts = []
for i, (lbl, key, unit, color) in enumerate(FOOTER_ITEMS):
    row = i // FOOTER_COLS
    col = i % FOOTER_COLS
    x = (col + 0.5) * CELL_W
    y_label = 0.90 if row == 0 else 0.42
    y_value = 0.72 if row == 0 else 0.24

    ax_foot.text(x, y_label, lbl,
                 transform=ax_foot.transAxes,
                 color=color, fontsize=6.3, fontfamily="monospace",
                 ha="center", va="top", alpha=0.6)
    t = ax_foot.text(x, y_value, "—",
                     transform=ax_foot.transAxes,
                     color=color, fontsize=10.6, fontfamily="monospace",
                     ha="center", va="top", fontweight="bold")
    val_texts.append((t, key, unit, color))

# =============================================================
# ANIMATION
# =============================================================
def update(_):
    with lock:
        setdata(l_t21,  data["temp_aht21"])
        setdata(l_t20,  data["temp_aht20"])
        setdata(l_tbmp, data["temp_bmp"])
        setdata(l_h21,  data["hum_aht21"])
        setdata(l_h20,  data["hum_aht20"])
        setdata(l_p,    data["pressure"])
        setdata(l_co2,  data["co2"])
        setdata(l_tvoc, data["tvoc"])
        setdata(l_pm1,  data["pm1"])
        setdata(l_pm25, data["pm25"])
        setdata(l_pm10, data["pm10"])

        draw_aqi_segments(ax_aqi, data["aqi"], aqi_seg_lines)
        ax_aqi.set_ylim(0.5, 5.5)
        ax_aqi.set_xlim(0, MAX_POINTS)

        autoscale(ax_temp,  data["temp_aht21"], data["temp_aht20"], data["temp_bmp"])
        autoscale(ax_hum,   data["hum_aht21"],  data["hum_aht20"])
        autoscale(ax_press, data["pressure"])
        autoscale(ax_co2,   data["co2"])
        autoscale(ax_tvoc,  data["tvoc"])
        autoscale(ax_pm,    data["pm1"], data["pm25"], data["pm10"])

        for t_obj, key, unit, base_color in val_texts:
            v = latest[key]
            if v is not None:
                t_obj.set_text(f"{v:.1f} {unit}" if unit else str(int(v)))
                if key == "aqi":
                    t_obj.set_color(aqi_color(v))
            else:
                t_obj.set_text("—")

        status_txt.set_text(status_msg[0])
        
        # Update date display
        current_date = datetime.now(LOCAL_TZ).strftime("%Y-%m-%d")
        date_txt.set_text(current_date)

    return tuple(aqi_seg_lines) + (
        l_t21, l_t20, l_tbmp, l_h21, l_h20,
        l_p, l_co2, l_tvoc, l_pm1, l_pm25, l_pm10,
        status_txt, date_txt
    )

# =============================================================
# MAIN
# =============================================================
if __name__ == "__main__":
    print("weather-room dashboard v2.8")
    print(f"Port: {SERIAL_PORT}  |  Baud: {BAUD_RATE}")
    print("Close window to quit.\n")

    threading.Thread(target=serial_reader, daemon=True).start()

    ani = FuncAnimation(fig, update, interval=UPDATE_MS,
                        blit=False, cache_frame_data=False)
    plt.show()