# Deploy

## Build Types

```bash
# Release
make clean && make -j$(nproc)

# Debug
make clean && CXXFLAGS="-O0 -g -DDEBUG" make -j$(nproc)
```

## Packaging

### AppImage
```bash
wget -O linuxdeploy-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy-x86_64.AppImage

mkdir -p AppDir/usr/bin
cp bin/elm327_console AppDir/usr/bin/

cat > AppDir/usr/share/applications/obd2-console.desktop << EOF
[Desktop Entry]
Name=OBD2 Console
Exec=elm327_console
Icon=utilities-terminal
Terminal=true
Type=Application
Categories=Utility;Diagnostic;
EOF

./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage
```

### Docker
```dockerfile
FROM ubuntu:22.04
RUN apt update && apt install -y build-essential libbluetooth-dev cmake
COPY . /app
WORKDIR /app
RUN make clean && make -j$(nproc)
CMD ["./bin/elm327_console"]
```

## Versioning

```bash
git tag v1.0.0
git push origin v1.0.0
```

## Release Checklist

- [ ] Compiles without errors
- [ ] Version in CMakeLists.txt and CHANGELOG.md updated
- [ ] `protocol.cache` excluded (`.gitignore`)
- [ ] Binary built in Release mode
- [ ] Tag created and pushed
