block_cipher = None

a = Analysis(
    ['src/main.py'],
    pathex=['src'],
    binaries=[],
    datas=[
        ('web', 'web')
    ],
    hiddenimports=[
        'uvicorn.logging',
        'uvicorn.loops.auto',
        'uvicorn.loops.asyncio',
        'uvicorn.protocols.http.auto',
        'uvicorn.protocols.http.h11_impl',
        'uvicorn.protocols.websockets.auto',
        'uvicorn.protocols.websockets.websockets_impl',
        'uvicorn.protocols.websockets.wsproto_impl',
        'uvicorn.lifespan.on',
        'uvicorn.lifespan.off',
    ],
    hookspath=[],
    runtime_hooks=[],
    excludes=[
        'pytest', 
        'IPython', 
        'jupyter', 
        'matplotlib', 
        'tkinter'
    ],
    cipher=block_cipher,
)

pyz = PYZ(
    a.pure, a.zipped_data, cipher=block_cipher
)

exe = EXE(
    pyz, a.scripts, [], exclude_binaries=True, name='main',
    debug=False, strip=False, upx=False, console=True
)

coll = COLLECT(
    exe, a.binaries, a.zipfiles, a.datas,
    strip=False, upx=False, name='runtime'
)