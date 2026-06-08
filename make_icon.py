import os
import shutil
from PIL import Image

SRC = r'C:\Users\QTJ-10\.cursor\projects\d-Workspace-FH6FocusKeeper\assets\c__Users_QTJ-10_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_ChatGPT_Image_2026_6_8__12_56_23-ddc38b92-5c21-47a3-b4e8-fe403e9e559a.png'
RES_DIR = r'd:\Workspace\FH6FocusKeeper\res'

# 1) Save the source material into res/
src_copy = os.path.join(RES_DIR, 'app_icon_source.png')
shutil.copyfile(SRC, src_copy)
print(f'Source saved: {src_copy}')

# 2) Load source (already a 1024x1024 square)
img = Image.open(SRC).convert('RGBA')

# 3) Generate high-quality multi-size ICO (each size rendered from the full-res image)
sizes = [16, 24, 32, 48, 64, 128, 256]
icons = [img.resize((s, s), Image.Resampling.LANCZOS) for s in sizes]

ico_path = os.path.join(RES_DIR, 'app.ico')
icons[-1].save(ico_path, format='ICO',
               sizes=[(s, s) for s in sizes],
               append_images=icons[:-1])

print(f'Icon saved: {ico_path} ({len(sizes)} sizes, {os.path.getsize(ico_path)} bytes)')
