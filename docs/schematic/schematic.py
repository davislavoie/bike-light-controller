"""Bike light controller schematic, drawn with schemdraw.

Run:  python docs/schematic/schematic.py
Writes one SVG per sheet next to this file. Sheets connect through net labels
(VBAT, +5V, +3V3, GND and the signal names in the KB2040 sheet).
"""
from pathlib import Path

import schemdraw
import schemdraw.elements as elm

schemdraw.use("svg")
OUT = Path(__file__).parent
FONT = "Geist, Helvetica, Arial, sans-serif"
INK = "#2d3142"
NET = "#c9531f"   # net-label colour


def drawing():
    d = schemdraw.Drawing(show=False)
    d.config(unit=2.5, fontsize=11, font=FONT, color=INK, lw=1.3, bgcolor="white")
    return d


def tag(name):
    return elm.Tag(width=0.16 * len(name) + 0.5).label(name, fontsize=9, color=NET).color(NET)


def net(d, at, name, direction="right", length=0.75):
    """Short stub + net-label flag at a point."""
    stub = getattr(elm.Line().at(at), direction)().length(length)
    d += stub
    d += getattr(tag(name).at(stub.end), direction)()


def gnd(d, at):
    d += elm.Ground().at(at)


def note(d, at, text):
    d += elm.Label().at(at).label(text, halign="left", fontsize=9)


# --------------------------------------------------------------- sheet 1: power
def sheet_power():
    d = drawing()
    # ---- Cross tube: battery -> master switch -> fuse -> Wago lever nuts
    d += (bt := elm.Battery().up().reverse().label("BT1\nM12 12V 3Ah\n9.0–12.6V", loc="bot", ofst=0.3))
    d += elm.Line().up().length(0.5)
    d += elm.Switch().right().label("SW1\nMASTER", loc="top")
    d += elm.Fuse().right().label("F1\n5A blade", loc="top")
    d += (wago := elm.Dot())
    d += elm.Label().at((wago.center[0] + 0.3, wago.center[1] + 0.35)).label("WAGO +", halign="left", fontsize=10)
    gnd(d, bt.start)

    # The Wagos are the Y splice: battery 12V and GND join the rear lamp's harness there.
    # The lamp's ground (black) lands on the GND Wago; tail (white) runs up to the box like the rest.
    lamp_pins = [("WHT tail", "LAMP_WHT"), ("BLK GND", "GND"), ("RED brake", "LAMP_RED"),
                 ("YEL turn*", "LAMP_YEL"), ("GRN turn*", "LAMP_GRN")]
    d += (lamp := elm.Ic(pins=[elm.IcPin(name=n, side="right", anchorname=f"W{k}")
                               for k, (n, _) in reversed(list(enumerate(lamp_pins)))],
                         pinspacing=0.8, edgepadH=0.4, size=(3.2, 4.4)).right().anchor("W0").at((4.5, -2))
          .label("Rear lamp\n(own harness)", loc="top", ofst=0.2))
    for k, (_, n) in enumerate(lamp_pins):
        net(d, getattr(lamp, f"W{k}"), n, "right", 0.5)

    # J1: harness from the Wagos up to the handlebar box (22 AWG power, 24 AWG signals)
    j1_pins = [("1 +12V", None, None), ("2 GND", "GND", "GND"), ("3 brake", "LAMP_RED", "REAR_BRAKE"),
               ("4 turn", "LAMP_YEL", "REAR_TURN_L"), ("5 turn", "LAMP_GRN", "REAR_TURN_R"), ("6 tail", "LAMP_WHT", "REAR_TAIL")]
    pins = ([elm.IcPin(name=n, side="left", anchorname=f"L{k}") for k, (n, _, _) in reversed(list(enumerate(j1_pins)))] +
            [elm.IcPin(name="", side="right", anchorname=f"R{k}") for k in reversed(range(len(j1_pins)))])
    d += (j1 := elm.Ic(pins=pins, pinspacing=0.8, edgepadH=0.4, size=(2.6, 5.2)).right().anchor("L0").at((10.5, 3))
          .label("J1  Nano-Fit 2×3", loc="top", ofst=0.2))
    d += elm.Line().at(wago.center).to(j1.L0)
    for k, (_, left, right) in enumerate(j1_pins[1:], start=1):
        net(d, getattr(j1, f"L{k}"), left, "left", 0.75)
        net(d, getattr(j1, f"R{k}"), right, "right", 0.75)

    # region labels + boundary at the connector
    xb = (j1.L0[0] + j1.R0[0]) / 2
    for y1, y2 in ((5.6, 4.6), (-1.5, -6.8)):   # above and below the J1 body, not through it
        d += elm.Line().at((xb, y1)).to((xb, y2)).linestyle("--").color("#9aa0ad").linewidth(0.8)
    d += elm.Label().at((xb - 0.4, 6.0)).label("CROSS TUBE", halign="right", fontsize=10, color="#7a8399")
    d += elm.Label().at((xb + 0.4, 6.0)).label("HANDLEBAR BOX", halign="left", fontsize=10, color="#7a8399")

    # ---- Handlebar box: VBAT rail, input cap right at the regulator pins, K7805W (1 VIN, 2 GND, 3 VOUT)
    d += elm.Line().right().at(j1.R0).length(1)
    d += (vbat := elm.Dot())
    net(d, vbat.center, "VBAT", "up", 0.6)
    d += elm.Line().right().at(vbat.center).length(3.5)
    d += (n_c := elm.Dot())
    d += elm.Capacitor(polar=True).down().label("C1\n100µF 35V\nlow-ESR", loc="bot")
    gnd(d, d.here)
    d += elm.Line().right().at(n_c.center).length(2)
    d += (u1 := elm.Ic(pins=[elm.IcPin(name="VIN", pin="1", side="left"),
                             elm.IcPin(name="GND", pin="2", side="bot"),
                             elm.IcPin(name="VOUT", pin="3", side="right")],
                       size=(3.4, 2.2))
          .anchor("VIN").label("U1  K7805W-500R3\n6.5–36V in · 5V 0.5A", loc="top", ofst=0.2))
    gnd(d, u1.GND)
    d += elm.Line().right().at(u1.VOUT).length(1.5)
    d += (n5 := elm.Dot())
    d += elm.Capacitor().down().label("C2\n22µF 25V\nX7R ceramic", loc="bot")
    gnd(d, d.here)
    net(d, n5.center, "+5V", "right", 1.0)

    # Battery sense divider -> ADC (no filter cap: firmware averages the readings)
    x0, y0 = j1.R0[0] + 3.5, -3.5
    d += tag("VBAT").left().at((x0 + 1.5, y0)).reverse()
    d += elm.Line().right().at((x0 + 1.5, y0)).length(0.5)
    d += elm.Resistor().right().label("R3\n100k", loc="top")
    d += (vs := elm.Dot())
    d += elm.Resistor().down().label("R4\n22k", loc="bot")
    gnd(d, d.here)
    net(d, vs.center, "VSENSE", "right", 1.5)

    note(d, (bt.start[0], -8.2),
         "Low-voltage cutoff is in firmware: below 10.0 V the last battery bar blinks (rear lamp has shut itself off); 9.4 V held for 5 s turns every light off and the RP2040 sleeps.\n"
         "Turn SW1 off after a cutoff. C1 and C2 mount right at U1's pins. VSENSE = VBAT × 22/122: 12.6 V reads 2.27 V, 9.0 V reads 1.62 V.   +5V feeds the KB2040 RAW pin.\n"
         "* The lamp listing doesn't say which of yellow/green is left: touch each to 12 V before crimping. Headlight connects on J2 (Nano-Fit 1×2), sheet 4; brake levers on J3 (Nano-Fit 2×2), sheet 3.\n"
         "J1 pin 2 carries the whole box's ground return, headlight included: use 20 AWG for J1's 12V and GND.")
    return d


# --------------------------------------------------------------- sheet 2: MCU
# top -> bottom on each side, in the board's physical header order (USB end at the top)
LEFT = [("D0/GP0", "LED_TURN_L"), ("D1/GP1", "LED_TURN_R"), ("GND", "GND"), ("GND", "GND"),
        ("D2/GP2", "BRAKE_L"), ("D3/GP3", "BRAKE_R"), ("D4/GP4", "HEAD_ON"), ("D5/GP5", "HEAD_STROBE"),
        ("D6/GP6", "CTL_BRAKE"), ("D7/GP7", "CTL_TURN_L"), ("D8/GP8", "CTL_TURN_R"), ("D9/GP9", "GATE_HEAD")]
RIGHT = [("RAW", "+5V"), ("GND", "GND"), ("RESET", None), ("3V3", None),
         ("A3/GP29", "VSENSE"), ("A2/GP28", "BRIGHT_HI"), ("A1/GP27", "CTL_TAIL"), ("A0/GP26", "BRIGHT_LO"),
         ("SCK/GP18", "LED_BATT_2"), ("MISO/GP20", "LED_BATT_4"), ("MOSI/GP19", "LED_BATT_3"), ("D10/GP10", "LED_BATT_1")]


def sheet_mcu():
    d = drawing()
    # schemdraw stacks side pins bottom-up, so feed them reversed
    pins = ([elm.IcPin(name=p, side="left", anchorname=f"L{i}") for i, (p, _) in reversed(list(enumerate(LEFT)))] +
            [elm.IcPin(name=p, side="right", anchorname=f"R{i}") for i, (p, _) in reversed(list(enumerate(RIGHT)))])
    d += (u := elm.Ic(pins=pins, pinspacing=0.75, edgepadH=0.5, size=(4.5, 9.5))
          .label("U2  Adafruit KB2040\nRP2040", loc="top", ofst=0.2))
    for i, (_, n) in enumerate(LEFT):
        if n is None:
            d += elm.NoConnect().at(getattr(u, f"L{i}"))
        elif n == "GND":
            d += elm.Line().left().at(getattr(u, f"L{i}")).length(0.75)
            d += elm.Ground().theta(270)  # points left, clear of the pins below
        else:
            net(d, getattr(u, f"L{i}"), n, "left", 0.75)
    for i, (_, n) in enumerate(RIGHT):
        at = getattr(u, f"R{i}")
        if n is None:
            d += elm.NoConnect().at(at)
        elif n == "GND":
            d += elm.Line().right().at(at).length(0.75)
            gnd(d, d.here)
        else:
            net(d, at, n, "right", 0.75)
    note(d, (u.L11[0] - 3, u.L11[1] - 2),
         "Every header GPIO is in use. Spare: Stemma QT (GP12/GP13); 3V3 out unused. Onboard NeoPixel on GP17 is unused.")
    return d


# --------------------------------------------------------------- sheet 3: handlebar HUD
# No external pull-ups or filter caps: inputs use the RP2040's internal pull-ups
# (INPUT_PULLUP) and are debounced / averaged in firmware.
def sheet_hud():
    d = drawing()
    for x, sig, lbl in [(2, "BRAKE_L", "SW2 brake lever L (NC)\nJ3 pins 1 / 2"), (8, "BRAKE_R", "SW3 brake lever R (NC)\nJ3 pins 3 / 4")]:
        d += elm.Dot().at((x, 0))
        net(d, (x, 0), sig, "left", 0.5)
        d += elm.Button(nc=True).down().at((x, 0)).label(lbl, loc="bot")
        gnd(d, d.here)

    # Headlight switch: ON-OFF-ON, common to GND, each throw to a pulled-up GPIO
    y = -5
    n_on, n_st = (0, y), (4, y)
    d += elm.Dot().at(n_on)
    d += elm.Dot().at(n_st)
    d += (sw := elm.SwitchSpdt2().up().anchor("a").at((2, y - 2.2))
          .label("SW4  ON-OFF-ON\nheadlight", loc="bot", ofst=(0, -0.2)))
    d += elm.Wire("|-").at(n_on).to(sw.b)
    d += elm.Wire("|-").at(n_st).to(sw.c)
    d += elm.Line().down().at(sw.a).length(0.4)
    gnd(d, d.here)
    net(d, n_on, "HEAD_ON", "left", 0.5)
    net(d, n_st, "HEAD_STROBE", "right", 0.5)

    # Headlight brightness: ON-OFF-ON, LOW / MED (centre) / HIGH
    n_lo, n_hi = (10, y), (14, y)
    d += elm.Dot().at(n_lo)
    d += elm.Dot().at(n_hi)
    d += (sw5 := elm.SwitchSpdt2().up().anchor("a").at((12, y - 2.2))
          .label("SW5  ON-OFF-ON\nbrightness", loc="bot", ofst=(0, -0.2)))
    d += elm.Wire("|-").at(n_lo).to(sw5.b)
    d += elm.Wire("|-").at(n_hi).to(sw5.c)
    d += elm.Line().down().at(sw5.a).length(0.4)
    gnd(d, d.here)
    net(d, n_lo, "BRIGHT_LO", "left", 0.5)
    net(d, n_hi, "BRIGHT_HI", "right", 0.5)

    # Indicator LEDs
    y = -11
    leds = [("LED_TURN_L", "D1 amber", "330Ω"), ("LED_TURN_R", "D2 amber", "330Ω"),
            ("LED_BATT_1", "D3 red", "330Ω"), ("LED_BATT_2", "D4 red", "330Ω"),
            ("LED_BATT_3", "D5 red", "330Ω"), ("LED_BATT_4", "D6 red", "330Ω")]
    for i, (sig, name, r) in enumerate(leds):
        x = 2.5 + i * 3.4
        d += elm.Dot().at((x, y))
        net(d, (x, y), sig, "left", 0.3)
        d += elm.Resistor().down().at((x, y)).label(r, loc="bot")
        d += elm.LED().down().label(name, loc="bot")
        gnd(d, d.here)

    note(d, (-1, y - 7.4),
         "Inputs use the RP2040 internal pull-ups (INPUT_PULLUP) and are debounced in firmware. BRIGHT_LO and BRIGHT_HI both HIGH = medium.\n"
         "Both levers plug into J3 (Nano-Fit 2×2): 1 BRAKE_L · 2 GND · 3 BRAKE_R · 4 GND.\n"
         "Brake = BRAKE_L and BRAKE_R both HIGH (levers squeezed, NC contacts open). A cut wire also reads HIGH, so the brake light fails on.\n"
         "HEAD_ON and HEAD_STROBE both HIGH = headlight off.  D3–D6 form the 4-bar battery gauge (D3 = first bar). Use diffused LEDs behind the PETG window.")
    return d


# --------------------------------------------------------------- sheet 4: light drivers
def nfet_with_gate(d, drain_at, q, sig, rg, rpd):
    """IRLB3813PbF (drain at drain_at) + 22Ω series gate resistor + 10k pulldown; source to GND."""
    # .right() puts drain on top, source below, gate on the right
    d += (qq := elm.NFet(bulk=False).right().anchor("drain").at(drain_at))
    d += elm.Label().at((qq.source[0] - 0.3, qq.source[1] + 0.2)).label(
        f"{q}\nIRLB3813PbF", halign="right", valign="bottom")
    d += elm.Line().down().at(qq.source).length(0.5)
    gnd(d, d.here)
    d += elm.Line().right().at(qq.gate).length(0.4)
    d += (ng := elm.Dot())
    d += elm.Resistor().right().label(f"{rg}\n22Ω", loc="top")
    d += tag(sig).right()
    d += elm.Resistor().down().at(ng.center).label(f"{rpd}\n10k", loc="bot")
    gnd(d, d.here)


def sheet_drivers():
    d = drawing()
    # Headlight: 2-wire lamp, low-side switched so it can be PWM-dimmed and strobed
    d += elm.Vdd().at((0, 2)).label("VBAT", fontsize=9, color=NET)
    d += elm.Lamp().down().at((0, 2)).label("HEADLIGHT\nJ2 Nano-Fit 1×2", loc="top")
    d += elm.Line().down().length(0.5)
    nfet_with_gate(d, d.here, "Q1", "GATE_HEAD", "R5", "R6")

    # Rear lamp: common ground, so each function is connected to 12V by a photorelay
    # (Toshiba TLP241A, DIP-4). Its output is an isolated switch, so it sits on the 12V
    # side like a jumper wire; the GPIO only lights its internal LED.
    chans = [("CTL_BRAKE", "REAR_BRAKE", "U3", "R7"), ("CTL_TURN_L", "REAR_TURN_L", "U4", "R8"),
             ("CTL_TURN_R", "REAR_TURN_R", "U5", "R9"), ("CTL_TAIL", "REAR_TAIL", "U6", "R10")]
    for i, (sig, out, u, r) in enumerate(chans):
        x = 9 + i * 12
        d += (pr := elm.Ic(pins=[elm.IcPin(name="LED+", pin="1", side="left", slot="2/2", anchorname="A"),
                                 elm.IcPin(name="LED−", pin="2", side="left", slot="1/2", anchorname="K"),
                                 elm.IcPin(name="", pin="4", side="right", slot="2/2", anchorname="O4"),
                                 elm.IcPin(name="", pin="3", side="right", slot="1/2", anchorname="O3")],
                           size=(3.0, 2.4)).right().at((x, -1))
              .label(f"{u}  TLP241A\nphotorelay", loc="bot", ofst=0.3))
        d += elm.Resistor().left().at(pr.A).label(f"{r}\n330Ω", loc="top")
        d += tag(sig).left().reverse()
        d += elm.Line().left().at(pr.K).length(0.5)
        gnd(d, d.here)
        net(d, pr.O4, "VBAT", "right", 0.75)
        net(d, pr.O3, out, "right", 0.75)

    note(d, (-2, -6.5),
         "Headlight: low-side, 25 kHz PWM for dimming and strobe (22 Ω gate resistor, 12 mA pin drive). Rear lamp: shared ground, so tail, brake and turns are switched on the 12V side, fully on/off (no PWM);\n"
         "the lamp runs its own breathing / brake-flash / sequential patterns. REAR_* nets leave the box on J1 (sheet 1); lamp ground sits on the GND Wago.\n"
         "R6 holds the headlight off while the MCU is unpowered. TLP241A: 40 V / 2 A output, LED trigger ≤ 3 mA; set GP6–GP8 and the tail pin to 8 mA drive in firmware. Output pins 3/4 are interchangeable.")
    return d


if __name__ == "__main__":
    for name, fn in [("1-power", sheet_power), ("2-mcu", sheet_mcu), ("3-hud", sheet_hud), ("4-drivers", sheet_drivers)]:
        path = OUT / f"sheet-{name}.svg"
        fn().save(str(path))
        print("wrote", path)
