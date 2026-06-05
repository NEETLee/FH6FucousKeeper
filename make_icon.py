from PIL import Image

img = Image.open(r'C:\Users\QTJ-10\.cursor\projects\d-Workspace-FH6FocusKeeper\assets\app_icon_v2.png')
img = img.convert('RGBA')

sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
icons = []
for size in sizes:
    resized = img.resize(size, Image.Resampling.LANCZOS)
    icons.append(resized)

ico_path = r'd:\Workspace\FH6FocusKeeper\res\app.ico'
icons[0].save(ico_path, format='ICO', sizes=[(s, s) for s in [16, 24, 32, 48, 64, 128, 256]], append_images=icons[1:])
print(f'Icon saved: {ico_path}')
