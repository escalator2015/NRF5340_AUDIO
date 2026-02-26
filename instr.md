cd ~/nrf5340_audio

./build.sh              # Build incremental (rápido)
./build.sh -p           # Build limpio desde cero
./build.sh -p -f        # Build limpio + flashear
./build.sh -d           # Build con símbolos de debug
./build.sh -h           # Ayuda

cd ~/nrf5340_audio

# El script ya estará allí (está en el repo)
./build.sh -p -f    # Primera compilación + flash


Si en algún momento necesitas cambiar la configuración de build y actualizar el backup:

# Regenerar backup
./build.sh -p

# Actualizar archivos de config en git
cp build/nrf5340_audio_sysbuild_cache.txt build_config_export/
# ... etc

git add build_config_export/
git commit -m "Actualizar configuración de build"
git push