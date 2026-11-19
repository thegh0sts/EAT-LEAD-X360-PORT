# tools/make_hint.py <out dir>: draws the "B Quit" menu hint (our own art, no game assets): a glossy red B button like the game's prompt
# buttons and "Quit" in white, at the size the game's "A Select" prompt has at 1080 output lines.
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
S = 4                      # drawn at 4x, then scaled down
H, D = 44, 40              # image height, button diameter (1080p pixels)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def button(face, letter, ink):
    d = D * S
    img = Image.new("RGBA", (d + 8 * S, d + 8 * S), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    o = 4 * S
    g.ellipse((o - 2 * S, o - 2 * S, o + d + 2 * S, o + d + 2 * S), fill=(10, 10, 10, 255))       # dark rim
    g.ellipse((o - 0.5 * S, o - 0.5 * S, o + d + 0.5 * S, o + d + 0.5 * S), fill=(200, 200, 200, 255))  # light ring
    g.ellipse((o + 1.5 * S, o + 1.5 * S, o + d - 1.5 * S, o + d - 1.5 * S), fill=face[0])
    # radial-ish shading: brighter towards the lower middle, dark edge
    for i in range(30, 0, -1):
        t = i / 30
        r = (d / 2 - 2 * S) * t
        cx, cy = o + d / 2, o + d / 2 + (1 - t) * d * 0.12
        c = tuple(int(face[1][k] + (face[0][k] - face[1][k]) * t) for k in range(3)) + (255,)
        g.ellipse((cx - r, cy - r, cx + r, cy + r), fill=c)
    # gloss on the top half
    gloss = Image.new("RGBA", img.size, (0, 0, 0, 0))
    gg = ImageDraw.Draw(gloss)
    gg.ellipse((o + d * 0.14, o + d * 0.06, o + d * 0.86, o + d * 0.52), fill=(255, 255, 255, 110))
    gloss = gloss.filter(ImageFilter.GaussianBlur(S * 1.2))
    img.alpha_composite(gloss)
    f = ImageFont.truetype(BOLD, int(d * 0.62))
    g = ImageDraw.Draw(img)
    box = g.textbbox((0, 0), letter, font=f)
    w, h = box[2] - box[0], box[3] - box[1]
    g.text((o + d / 2 - w / 2 - box[0], o + d / 2 - h / 2 - box[1]), letter, font=f, fill=ink)
    return img

def hint(btn, text, out):
    f = ImageFont.truetype(FONT, 34 * S)
    tmp = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    box = tmp.textbbox((0, 0), text, font=f)
    tw = box[2] - box[0]
    W = D + 8 + 6 + tw // S + 4
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    img.alpha_composite(btn, (0, (H * S - btn.size[1]) // 2))
    tx, ty = (D + 8 + 4) * S, (H * S - (box[3] - box[1])) // 2 - box[1]
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((tx + S, ty + S), text, font=f, fill=(0, 0, 0, 200))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(S)))
    ImageDraw.Draw(img).text((tx, ty), text, font=f, fill=(242, 242, 242, 255))
    img = img.resize((W, H), Image.LANCZOS)
    img.save(out)
    print(out, img.size)

red = button(((235, 40, 40, 255), (150, 0, 0)), "B", (120, 0, 0, 255))
hint(red, "Quit", sys.argv[1] + "/b_quit.png")
