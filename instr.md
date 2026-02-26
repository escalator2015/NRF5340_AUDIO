cd ~/nrf5340_audio

./build.sh              # Build incremental (rápido)
./build.sh -p           # Build limpio desde cero
./build.sh -p -f        # Build limpio + flashear
./build.sh -d           # Build con símbolos de debug
./build.sh -h           # Ayuda

cd ~/nrf5340_audio

# El script ya estará allí (está en el repo)
./build.sh -p -f    # Primera compilación + flash
