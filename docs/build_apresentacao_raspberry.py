"""Gera a apresentacao AruaTech: Adequacao ao Raspberry Pi (arquitetura hibrida).

Usa python-pptx (sem rede, sem LibreOffice). Paleta AruaTech PEX.
Saida: docs/apresentacao-adequacao-raspberry.pptx
"""
import os
import sys

from pptx import Presentation
from pptx.util import Inches, Pt, Emu
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR

# ---- Paleta AruaTech (PEX) ----
PETROLEO = RGBColor(0x02, 0x30, 0x43)
TEAL = RGBColor(0x29, 0x78, 0x97)
VERDE_AGUA = RGBColor(0xB1, 0xD1, 0xC9)
BRANCO = RGBColor(0xFF, 0xFF, 0xFF)
CINZA = RGBColor(0xE2, 0xE8, 0xF0)
CINZA_TXT = RGBColor(0x33, 0x41, 0x55)
LARANJA = RGBColor(0xF5, 0x9E, 0x0B)

SLIDE_W = Inches(13.333)
SLIDE_H = Inches(7.5)

prs = Presentation()
prs.slide_width = SLIDE_W
prs.slide_height = SLIDE_H
BLANK = prs.slide_layouts[6]


def bg(slide, color):
    slide.background.fill.solid()
    slide.background.fill.fore_color.rgb = color


def rect(slide, x, y, w, h, color):
    from pptx.enum.shapes import MSO_SHAPE
    shp = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, x, y, w, h)
    shp.fill.solid()
    shp.fill.fore_color.rgb = color
    shp.line.fill.background()
    shp.shadow.inherit = False
    return shp


def textbox(slide, x, y, w, h, lines, align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP):
    """lines: list of (text, size, bold, color, space_after_pt)."""
    tb = slide.shapes.add_textbox(x, y, w, h)
    tf = tb.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    for i, (text, size, bold, color, sa) in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.space_after = Pt(sa)
        run = p.add_run()
        run.text = text
        run.font.size = Pt(size)
        run.font.bold = bold
        run.font.color.rgb = color
        run.font.name = "DM Sans"
    return tb


def bullets(slide, x, y, w, h, items, size=18, color=CINZA_TXT, gap=10):
    tb = slide.shapes.add_textbox(x, y, w, h)
    tf = tb.text_frame
    tf.word_wrap = True
    for i, it in enumerate(items):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.space_after = Pt(gap)
        run = p.add_run()
        run.text = "•  " + it
        run.font.size = Pt(size)
        run.font.color.rgb = color
        run.font.name = "DM Sans"
    return tb


def header(slide, kicker, title):
    rect(slide, 0, 0, SLIDE_W, Inches(1.35), PETROLEO)
    rect(slide, Inches(0.6), Inches(0.42), Inches(0.12), Inches(0.55), LARANJA)
    textbox(slide, Inches(0.9), Inches(0.25), Inches(11.5), Inches(1.0), [
        (kicker, 12, True, VERDE_AGUA, 2),
        (title, 26, True, BRANCO, 0),
    ])


# ── Slide 1: Capa ──────────────────────────────────────────────────────────
s = prs.slides.add_slide(BLANK)
bg(s, PETROLEO)
rect(s, 0, Inches(6.9), SLIDE_W, Inches(0.6), TEAL)
rect(s, Inches(0.9), Inches(2.2), Inches(0.18), Inches(1.9), LARANJA)
textbox(s, Inches(1.25), Inches(2.15), Inches(11), Inches(3), [
    ("ARUATECH · USV-AM", 16, True, VERDE_AGUA, 8),
    ("Adequacao ao Raspberry Pi", 44, True, BRANCO, 6),
    ("Arquitetura hibrida RPi 4 + ESP32 para navegacao fluvial autonoma", 20, False, CINZA, 0),
])
textbox(s, Inches(1.25), Inches(6.95), Inches(11), Inches(0.5),
        [("Veiculo Fluvial Nao Tripulado · Amazonia", 12, False, BRANCO, 0)])

# ── Slide 2: Motivacao da arquitetura hibrida ───────────────────────────────
s = prs.slides.add_slide(BLANK)
bg(s, BRANCO)
header(s, "POR QUE HIBRIDO", "O que motivou a escolha RPi 4 + ESP32")
textbox(s, Inches(0.9), Inches(1.7), Inches(11.5), Inches(0.6), [
    ("Um so cerebro nao atende os dois regimes do projeto: tempo real duro e "
     "computacao rica. A divisao resolve a tensao.", 16, False, CINZA_TXT, 0)])
# duas colunas
rect(s, Inches(0.9), Inches(2.5), Inches(5.5), Inches(4.2), VERDE_AGUA)
textbox(s, Inches(1.2), Inches(2.7), Inches(5.0), Inches(0.6),
        [("ESP32 sozinho nao bastava", 18, True, PETROLEO, 4)])
bullets(s, Inches(1.2), Inches(3.3), Inches(5.0), Inches(3.2), [
    "Sem folga de CPU/RAM para visao, rotas ricas e nuvem.",
    "Pilha WiFi/TLS competindo com o loop de controle.",
    "Dificil evoluir para 4G, camera e logica de missao.",
], size=15, color=PETROLEO)
rect(s, Inches(6.9), Inches(2.5), Inches(5.5), Inches(4.2), TEAL)
textbox(s, Inches(7.2), Inches(2.7), Inches(5.0), Inches(0.6),
        [("RPi 4 sozinho tambem nao", 18, True, BRANCO, 4)])
bullets(s, Inches(7.2), Inches(3.3), Inches(5.0), Inches(3.2), [
    "Sem GPIO/PWM deterministico: Linux nao e tempo real.",
    "Jitter de agendamento arrisca controle de motores.",
    "Ponto unico de falha para a autonomia critica.",
], size=15, color=BRANCO)
textbox(s, Inches(0.9), Inches(6.85), Inches(11.5), Inches(0.5), [
    ("Decisao: cada camada faz o que faz melhor — ESP32 no controle em tempo "
     "real, RPi 4 na missao e na nuvem.", 14, True, TEAL, 0)])

# ── Slide 3: Conexao / comunicacao entre os dois ────────────────────────────
s = prs.slides.add_slide(BLANK)
bg(s, BRANCO)
header(s, "COMO SE FALAM", "Conceitos de conexao e comunicacao escolhidos")
# fluxo em 3 caixas
box_y = Inches(2.0)
rect(s, Inches(0.9), box_y, Inches(3.5), Inches(1.6), PETROLEO)
textbox(s, Inches(1.0), box_y, Inches(3.3), Inches(1.6),
        [("Nuvem", 16, True, VERDE_AGUA, 3), ("Firebase RTDB + Auth", 13, False, BRANCO, 0)],
        align=PP_ALIGN.CENTER, anchor=MSO_ANCHOR.MIDDLE)
rect(s, Inches(4.9), box_y, Inches(3.5), Inches(1.6), TEAL)
textbox(s, Inches(5.0), box_y, Inches(3.3), Inches(1.6),
        [("Raspberry Pi 4", 16, True, BRANCO, 3),
         ("Missao, nuvem, daemon Python", 13, False, BRANCO, 0)],
        align=PP_ALIGN.CENTER, anchor=MSO_ANCHOR.MIDDLE)
rect(s, Inches(8.9), box_y, Inches(3.5), Inches(1.6), LARANJA)
textbox(s, Inches(9.0), box_y, Inches(3.3), Inches(1.6),
        [("ESP32", 16, True, PETROLEO, 3),
         ("Sensores, motores, LOS", 13, False, PETROLEO, 0)],
        align=PP_ALIGN.CENTER, anchor=MSO_ANCHOR.MIDDLE)
textbox(s, Inches(4.9), Inches(1.65), Inches(3.5), Inches(0.35),
        [("WiFi / 4G  (internet)", 11, True, CINZA_TXT, 0)], align=PP_ALIGN.CENTER)
textbox(s, Inches(8.9), Inches(1.65), Inches(3.5), Inches(0.35),
        [("UART 115200  (JSON-lines)", 11, True, CINZA_TXT, 0)], align=PP_ALIGN.CENTER)
bullets(s, Inches(0.9), Inches(4.0), Inches(11.5), Inches(3.0), [
    "Enlace fisico RPi<->ESP32: UART a 115200 bps, 3 fios (TX/RX cruzados + GND comum), 3.3V direto, sem conversor de nivel.",
    "Protocolo: linhas JSON (JSON-lines) com command_id para deduplicacao — comando processado uma unica vez.",
    "Sentido nuvem->veiculo: RPi escuta /command no RTDB e repassa por UART (set_destination, emergency_stop).",
    "Sentido veiculo->nuvem: ESP32 envia telemetria/eventos por UART; o daemon do RPi publica em /telemetry, /status, /path, /logs.",
    "Presenca e resiliencia: watchdog do link com reconexao (backoff), heartbeat, e status/online=false no shutdown.",
], size=14.5, gap=9)

# ── Slide 4: Invariante de autonomia ────────────────────────────────────────
s = prs.slides.add_slide(BLANK)
bg(s, PETROLEO)
rect(s, Inches(0.9), Inches(1.0), Inches(0.18), Inches(1.4), LARANJA)
textbox(s, Inches(1.25), Inches(0.95), Inches(11), Inches(1.5), [
    ("INVARIANTE DE PROJETO", 14, True, VERDE_AGUA, 6),
    ("Desconectar o RPi deixa o ESP32 navegando sozinho", 30, True, BRANCO, 0),
])
bullets(s, Inches(1.25), Inches(2.9), Inches(11), Inches(3.5), [
    "A missao (rota por waypoints, LOS, desvio de obstaculo) roda inteira no ESP32.",
    "Sem RPi presente, o ESP32 bufferiza telemetria localmente em LittleFS e segue a rota.",
    "A camada de nuvem existe para monitorar e comandar — nunca para pilotar.",
    "Reconexao: o RPi retoma a publicacao e faz flush do buffer, sem perder o historico.",
], size=17, color=CINZA, gap=12)
textbox(s, Inches(1.25), Inches(6.5), Inches(11), Inches(0.6), [
    ("Confiabilidade em rios amazonicos com conectividade intermitente.", 15, True, VERDE_AGUA, 0)])

# ── Slide 5: Ganhos do modelo hibrido ───────────────────────────────────────
s = prs.slides.add_slide(BLANK)
bg(s, BRANCO)
header(s, "RESULTADO", "Ganhos obtidos com o modelo hibrido")
cards = [
    ("Tempo real preservado", "Controle de motores e sensores sem jitter de SO — determinismo do ESP32."),
    ("Autonomia garantida", "Veiculo navega mesmo offline; nuvem e opcional para a missao."),
    ("Computacao rica no RPi", "Espaco para visao, 4G, logica de missao e integracao com Firebase."),
    ("Evolucao desacoplada", "Trocar/atualizar a camada de nuvem sem tocar no firmware critico."),
    ("Resiliencia de link", "Watchdog, reconexao com backoff e presenca no RTDB."),
    ("Custo e reuso", "Aproveita o ESP32 existente; RPi adiciona capacidade sob demanda."),
]
cx = [Inches(0.9), Inches(4.72), Inches(8.54)]
cy = [Inches(1.75), Inches(4.35)]
i = 0
for row in range(2):
    for col in range(3):
        title, body = cards[i]
        x, y = cx[col], cy[row]
        rect(s, x, y, Inches(3.6), Inches(2.35), CINZA)
        rect(s, x, y, Inches(3.6), Inches(0.12), TEAL)
        textbox(s, x + Inches(0.2), y + Inches(0.25), Inches(3.2), Inches(2.0), [
            (title, 16, True, PETROLEO, 6),
            (body, 12.5, False, CINZA_TXT, 0),
        ])
        i += 1

# ── Slide 6: Fechamento ─────────────────────────────────────────────────────
s = prs.slides.add_slide(BLANK)
bg(s, TEAL)
rect(s, Inches(0.9), Inches(2.6), Inches(0.18), Inches(1.6), LARANJA)
textbox(s, Inches(1.25), Inches(2.5), Inches(11), Inches(2.5), [
    ("ARUATECH · USV-AM", 15, True, PETROLEO, 8),
    ("Hibrido por design, autonomo por principio", 34, True, BRANCO, 6),
    ("ESP32 pilota. RPi 4 pensa e conecta. O rio nunca para a missao.", 18, False, PETROLEO, 0),
])

out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)))
out_path = os.path.join(out_dir, "apresentacao-adequacao-raspberry.pptx")
prs.save(out_path)

status_path = os.path.join(out_dir, "_pptx_build_status.txt")
with open(status_path, "w", encoding="utf-8") as fh:
    fh.write("OK slides=%d path=%s bytes=%d\n" % (
        len(prs.slides._sldIdLst), out_path, os.path.getsize(out_path)))
print("OK", out_path)
