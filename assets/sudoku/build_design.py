"""中文：拆分并量化获批设计素材，导出设备原生设计稿。
English: split and quantize approved artwork into native device design exports.
"""
from pathlib import Path
import json
import hashlib
import shutil
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent.parent
W, H = 684, 1216
LEVELS = [17 * i for i in range(16)]
FONT = str(REPO / 'assets/fonts/HarmonyOS_Sans_SC_Regular.ttf')
SERIF = 'C:/Windows/Fonts/simsun.ttc'
NAMES = ['reading', 'blink', 'look_up', 'thinking', 'wave_low', 'wave_high']
MANIFEST = []


def gray16(im):
    return im.convert('L').point([min(15, (v + 8) // 17) * 17 for v in range(256)])


def save_png(im, relative, role):
    path = ROOT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    # 中文：透明素材用16阶亮度与二值透明度；不挖空白色肚皮。
    # English: transparent sprites retain 16 luminances and binary alpha, including opaque white bellies.
    if im.mode == 'RGBA':
        alpha = im.getchannel('A').point(lambda v: 255 if v >= 128 else 0)
        lum = gray16(im)
        out = Image.merge('RGBA', (lum, lum, lum, alpha))
        out.save(path, optimize=True)
    else:
        lum = gray16(im)
        out = lum.point(lambda v: v // 17).convert('P')
        out.putpalette([c for v in LEVELS for c in (v, v, v)] + [0] * (768 - 48))
        out.save(path, bits=4, optimize=True)
    MANIFEST.append({'file': relative, 'width': im.width, 'height': im.height,
                     'role': role, 'bytes': path.stat().st_size,
                     'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})


def font(size, serif=False):
    return ImageFont.truetype(SERIF if serif else FONT, size)


def text(im, xy, value, size=24, fill=0, center=False, serif=False, bold=False):
    draw = ImageDraw.Draw(im)
    f = font(size, serif)
    if center:
        xy = (round(xy[0] - draw.textlength(value, font=f) / 2), xy[1])
    draw.text(xy, value, font=f, fill=fill, anchor='lt', stroke_width=1 if bold else 0)


def paste(im, asset, xy):
    if asset.mode == 'RGBA':
        im.paste(asset.convert('L'), xy, asset.getchannel('A'))
    else:
        im.paste(asset.convert('L'), xy)


def button(im, rect, label, primary=False, size=28):
    x, y, width, height = rect
    ImageDraw.Draw(im).rounded_rectangle((x, y, x+width-1, y+height-1), radius=8,
                                        fill=0 if primary else 255, outline=0 if primary else 153, width=1)
    text(im, (x+width/2, y+(height-size)//2-1), label, size, 255 if primary else 0, center=True)


source = Image.open(ROOT / 'sources/approved-concept.png').convert('L')
mount_source = Image.open(ROOT / 'sources/mountains-generated.png').convert('L')
mountains = Image.new('L', (684, 160), 255)
mount_fit = ImageOps.contain(mount_source, (560, 160), Image.Resampling.LANCZOS)
mountains.paste(mount_fit, (684-mount_fit.width, 0))
mountains = gray16(mountains)
save_png(mountains, 'assets/mountains_header_684x160.png', 'shared header artwork')
save_png(Image.new('L', (684, 1216), 255), 'assets/paper_684x1216.png', 'plain white reference; render fill in firmware')

# 中文：沿用获批图中的标识和线条图标，仅裁切、等比缩放。
# English: crop and proportionally resize the approved wordmark and line icons.
logo_src = source.crop((40, 32, 258, 103))
logo = Image.new('L', (180, 58), 255)
logo_fit = ImageOps.contain(logo_src, logo.size, Image.Resampling.LANCZOS)
logo.paste(logo_fit, ((180-logo_fit.width)//2, (58-logo_fit.height)//2))
save_png(logo, 'assets/wordmark_180x58.png', 'approved PicoNano wordmark crop')
icons = {}
for name, box in [('undo', (96, 1165, 161, 1225)), ('erase', (324, 1165, 387, 1225)),
                  ('notes', (551, 1164, 615, 1225)), ('menu', (782, 1165, 847, 1225))]:
    icon = Image.new('L', (48, 48), 255)
    crop = ImageOps.contain(source.crop(box), (42, 42), Image.Resampling.LANCZOS)
    icon.paste(crop, ((48-crop.width)//2, (48-crop.height)//2))
    icons[name] = gray16(icon)
    save_png(icon, f'assets/icon_{name}_48.png', 'approved concept icon crop')

sheet = Image.open(ROOT / 'sources/panda-generated.png').convert('RGBA')
cells = []
for i in range(6):
    col, row = i % 3, i // 3
    cell = sheet.crop((round(col*sheet.width/3), round(row*sheet.height/2),
                       round((col+1)*sheet.width/3), round((row+1)*sheet.height/2)))
    alpha = cell.getchannel('A').point(lambda v: 255 if v >= 128 else 0)
    box = alpha.getbbox()
    assert box
    cells.append(cell.crop(box))
scale = 44 / max(c.height for c in cells)
sprites = {}
for name, cell in zip(NAMES, cells):
    logical = Image.new('RGBA', (48, 48), (255, 255, 255, 0))
    scaled = cell.resize((round(cell.width*scale), round(cell.height*scale)), Image.Resampling.NEAREST)
    # 中文：用脚底中心对齐，而不是按每帧头部重新居中。
    # English: align using the feet center and fixed baseline rather than the head bounds.
    foot = scaled.getchannel('A').crop((0, max(0, scaled.height-5), scaled.width, scaled.height))
    footbox = foot.point(lambda v: 255 if v >= 128 else 0).getbbox()
    anchor = (footbox[0] + footbox[2]) / 2 if footbox else scaled.width / 2
    logical.paste(scaled, (round(24-anchor), 46-scaled.height))
    for size in (96, 144):
        sprite = logical.resize((size, size), Image.Resampling.NEAREST)
        lum = gray16(sprite)
        alpha = sprite.getchannel('A').point(lambda v: 255 if v >= 128 else 0)
        sprite = Image.merge('RGBA', (lum, lum, lum, alpha))
        sprites[name, size] = sprite
        save_png(sprite, f'assets/panda_{name}_{size}.png', 'aligned pixel panda animation pose')

atlas = Image.new('RGBA', (96*6, 96), (255, 255, 255, 0))
for i, name in enumerate(NAMES):
    atlas.paste(sprites[name, 96], (i*96, 0))
save_png(atlas, 'assets/panda_atlas_576x96.png', 'six aligned frames in NAMES order')

GIVENS = ['530070000','600195000','098000060','800060003','400803001',
          '700020006','060000280','000419005','000080079']
SOLUTION = ['534678912','672195348','198342567','859761423','426853791',
            '713924856','961537284','287419635','345286179']
bank_path = ROOT/'bank/puzzles.json'
if bank_path.exists():
    puzzle = next(r for r in json.loads(bank_path.read_text(encoding='utf-8'))['records'] if r['difficulty'] == 'medium')
    GIVENS = [puzzle['grid'][r*9:r*9+9] for r in range(9)]
    SOLUTION = [puzzle['solution'][r*9:r*9+9] for r in range(9)]
    (ROOT/'screen_puzzle.json').write_text(json.dumps({'id': puzzle['id'], 'difficulty': puzzle['difficulty'],
        'grid': puzzle['grid'], 'solution': puzzle['solution']}, indent=2), encoding='utf-8')
empty = [(r, c) for r in range(9) for c in range(9) if GIVENS[r][c] == '0']
FILLED = set(empty[3::6][:8])
ACTIVE = next(p for p in empty if p not in FILLED)
NOTES = {}
for r, c in reversed(empty):
    if (r, c) in FILLED or (r, c) == ACTIVE:
        continue
    peers = {(r, j) for j in range(9)} | {(i, c) for i in range(9)} | {(r//3*3+i, c//3*3+j) for i in range(3) for j in range(3)}
    used = {int(SOLUTION[i][j] if (i,j) in FILLED else GIVENS[i][j]) for i,j in peers}
    values = sorted(set(range(1, 10))-used)
    if 2 <= len(values) <= 4:
        NOTES[r, c] = values
    if len(NOTES) == 2:
        break
LAYOUT = {'screen': [684, 1216], 'grid': [36, 216, 612, 612], 'cell_size': 68,
          'panda': [536, 100, 96, 96], 'tools': [], 'number_keys': [],
          'difficulty_rows': [], 'new_game_button': [36, 1006, 612, 72],
          'completion_buttons': [[36, 944, 612, 64], [36, 1028, 612, 64], [36, 1112, 612, 52]]}


def base(title, subtitle, panda='reading', back=False):
    im = Image.new('L', (W, H), 255)
    paste(im, mountains, (0, 0))
    paste(im, logo, (32, 12))
    ImageDraw.Draw(im).line((36, 76, 284, 76), fill=204, width=1)
    text(im, (36, 98), title, 52, serif=True, bold=True)
    text(im, (36, 173), subtitle, 23, 85)
    if panda:
        paste(im, sprites[panda, 96], (536, 100))
    if back:
        text(im, (600, 27), '返回', 22, 51, center=True)
    return im


def grid(im, complete=False):
    draw = ImageDraw.Draw(im)
    x, y, cell = 36, 216, 68
    for r in range(9):
        for c in range(9):
            given = GIVENS[r][c] != '0'
            if given or complete or (r, c) in FILLED:
                number = SOLUTION[r][c]
                text(im, (x+c*cell+34, y+r*cell+16), number, 38,
                     0 if given else 68, center=True, bold=given)
            elif (r, c) in NOTES:
                for n in NOTES[r, c]:
                    nr, nc = (n-1)//3, (n-1)%3
                    text(im, (x+c*cell+12+nc*21, y+r*cell+7+nr*20), str(n), 16, 85, center=True)
    for n in range(1, 9):
        width, ink = (3, 0) if n % 3 == 0 else (1, 119)
        draw.line((x+n*cell, y, x+n*cell, y+612), fill=ink, width=width)
        draw.line((x, y+n*cell, x+612, y+n*cell), fill=ink, width=width)
    draw.rectangle((x, y, x+611, y+611), outline=0, width=3)
    if not complete:
        r, c = ACTIVE
        draw.rectangle((x+c*cell+3, y+r*cell+3, x+(c+1)*cell-3, y+(r+1)*cell-3), outline=0, width=3)


game = base('数独', '中等 · 12分钟')
grid(game)
for i, (name, label) in enumerate([('undo', '撤销'), ('erase', '擦除'), ('notes', '笔记'), ('menu', '菜单')]):
    cx = 36 + 153*i + 76
    paste(game, icons[name], (cx-24, 846))
    text(game, (cx, 901), label, 23, 68, center=True)
    LAYOUT['tools'].append({'action': name, 'rect': [36+153*i, 838, 153, 94]})
for n in range(1, 10):
    x = 36 + ((n-1) % 3)*210
    y = 948 + ((n-1)//3)*76
    button(game, (x, y, 192, 64), str(n), size=38)
    LAYOUT['number_keys'].append({'number': n, 'rect': [x, y, 192, 64]})
text(game, (342, 1183), '已保存', 18, 102, center=True)
save_png(game, 'screens/02_playing_684x1216.png', 'native gameplay design; fixed valid example puzzle')

choose = base('新游戏', '选一个难度，开始新的思考', panda=None, back=True)
paste(choose, sprites['look_up', 144], (270, 244))
text(choose, (342, 423), '来一局，慢慢想。', 28, 51, center=True, serif=True)
draw = ImageDraw.Draw(choose)
for i, (label, desc) in enumerate([('简单', '直接排除，轻松热身'), ('中等', '区块排除，耐心推理'),
                                   ('困难', '结合数对，深入推演'), ('极难', '稳住思路，挑战复杂推理')]):
    y = 484 + i*110
    draw.rounded_rectangle((36, y, 647, y+93), radius=8, fill=238 if i == 1 else 255,
                           outline=0 if i == 1 else 187, width=2 if i == 1 else 1)
    text(choose, (60, y+12), label, 28, bold=i == 1)
    text(choose, (60, y+55), desc, 21, 85)
    draw.ellipse((598, y+34, 622, y+58), outline=0 if i == 1 else 136, width=2)
    if i == 1:
        draw.ellipse((604, y+40, 616, y+52), fill=0)
    LAYOUT['difficulty_rows'].append({'difficulty': label, 'rect': [36, y, 612, 94]})
text(choose, (342, 946), '当前棋局仍保留，确认开始后才会替换', 22, 85, center=True)
button(choose, (36, 1006, 612, 72), '开始新游戏', True)
text(choose, (342, 1120), '返回当前棋局', 25, 51, center=True)
text(choose, (342, 1183), '退出此页不会丢失进度', 18, 102, center=True)
save_png(choose, 'screens/01_new_game_684x1216.png', 'difficulty selector while a game exists; selecting alone does not reset')

win = base('解开了', '中等 · 12分钟 · 提示0次', 'wave_high')
grid(win, complete=True)
text(win, (342, 858), '这一局，完成了。', 28, 34, center=True, serif=True)
text(win, (342, 903), '棋盘已保留，可以慢慢回看', 21, 85, center=True)
button(win, (36, 944, 612, 64), '同难度再来一局', True, 27)
button(win, (36, 1028, 612, 64), '选择难度', size=27)
text(win, (342, 1127), '返回首页', 24, 51, center=True)
text(win, (342, 1183), '通关记录已保存', 18, 102, center=True)
save_png(win, 'screens/03_completed_684x1216.png', 'completed puzzle retained until user explicitly starts a next game')

# 中文：补充有进行中棋局时的放弃确认，取消保留旧棋局。
# English: supplement the discard confirmation; cancel retains the current game.
confirm = game.copy()
ImageDraw.Draw(confirm).rectangle((24, 392, 660, 802), fill=255, outline=0, width=2)
text(confirm, (342, 437), '开始新的中等棋局？', 32, center=True, serif=True)
text(confirm, (342, 505), '当前未完成的进度将被替换。', 24, 68, center=True)
text(confirm, (342, 548), '取消后，仍可继续这一局。', 24, 68, center=True)
button(confirm, (56, 639, 276, 72), '保留当前棋局', size=24)
button(confirm, (352, 639, 276, 72), '确认新游戏', True, 24)
save_png(confirm, 'screens/04_confirm_new_game_684x1216.png', 'supplemental confirmation; no dimming full-screen refresh implied')

overview = Image.new('L', (W*3+48, H+64), 255)
for i, (im, label) in enumerate([(choose, '新游戏 · 选择难度'), (game, '游戏中 · 自动续局'), (win, '通关 · 保留棋盘')]):
    text(overview, (i*(W+24)+342, 16), label, 27, center=True)
    overview.paste(gray16(im), (i*(W+24), 64))
save_png(overview, 'previews/three_screens.png', 'three native screens side by side; not a device framebuffer')

poses = Image.new('L', (6*160, 210), 255)
for i, name in enumerate(NAMES):
    paste(poses, sprites[name, 144], (i*160+8, 12))
    text(poses, (i*160+80, 171), ['读书', '眨眼', '抬头', '思考', '举爪一', '举爪二'][i], 20, center=True)
save_png(poses, 'previews/panda_poses.png', 'pose contact sheet; not a device framebuffer')
frames = []
for name in ['reading', 'blink', 'reading', 'look_up', 'reading', 'thinking', 'reading', 'wave_low', 'wave_high', 'wave_low', 'reading']:
    frame = Image.new('L', (144, 144), 255)
    paste(frame, sprites[name, 144], (0, 0))
    frame = gray16(frame).point(lambda v: v//17).convert('P')
    frame.putpalette([c for v in LEVELS for c in (v, v, v)] + [0]*(768-48))
    frames.append(frame)
frames[0].save(ROOT/'previews/panda_motion.gif', save_all=True, append_images=frames[1:],
               duration=[1500, 900, 1000, 1100, 1200, 1100, 1200, 900, 1100, 900, 1800], loop=0, optimize=False)

animation = {'frame_order': NAMES, 'logical_canvas': [48, 48], 'baseline_y': 46,
             'exports': [96, 144], 'enter': ['reading', 'look_up', 'reading'],
             'hint': ['reading', 'thinking', 'reading'],
             'complete': ['reading', 'wave_low', 'wave_high', 'wave_low', 'reading'],
             'policy': 'event-only, play once, then static; skip if input or display is busy',
             'draft_frame_ms': 1000, 'timing_status': 'design value, requires real-panel validation'}
(ROOT/'layout.json').write_text(json.dumps(LAYOUT, ensure_ascii=False, indent=2), encoding='utf-8')
(ROOT/'animation.json').write_text(json.dumps(animation, ensure_ascii=False, indent=2), encoding='utf-8')
(ROOT/'manifest.json').write_text(json.dumps({'gray_levels': LEVELS, 'black_index': 0, 'white_index': 15,
    'opaque_png': 'indexed palette, 4-bit, 16 fixed grays', 'transparent_png': 'RGBA, 16 gray RGB values, binary alpha',
    'assets': MANIFEST}, ensure_ascii=False, indent=2), encoding='utf-8')
print(f'Exported {len(MANIFEST)} PNGs at {ROOT}')
