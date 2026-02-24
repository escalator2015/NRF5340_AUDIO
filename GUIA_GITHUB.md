# Guía para Crear y Mantener Repositorio GitHub - Proyecto ALTIUM48k

## 📋 Requisitos Previos

- Tener una cuenta en [GitHub](https://github.com)
- Tener Git instalado en tu sistema
- Terminal abierta en la carpeta del proyecto

## 🚀 Paso 1: Crear Repositorio en GitHub

1. Ve a [GitHub](https://github.com) e inicia sesión
2. Haz clic en el botón **"+"** en la esquina superior derecha
3. Selecciona **"New repository"**
4. Configura el repositorio:
   - **Repository name**: `ALTIUM48k` (o el nombre que prefieras)
   - **Description**: "ESP32 Audio DSP Processor - ALTIUM 48kHz Project"
   - **Visibility**: Elige **Private** o **Public** según tus necesidades
   - ⚠️ **NO** marques "Initialize this repository with a README"
   - ⚠️ **NO** agregues .gitignore ni licencia (lo haremos localmente)
5. Haz clic en **"Create repository"**

## 📝 Paso 2: Configurar Git Localmente

Abre la terminal en la carpeta del proyecto y configura tu identidad:

```bash
git config --global user.name "Tu Nombre"
git config --global user.email "tu-email@ejemplo.com"
```

## 📂 Paso 3: Crear archivo .gitignore

Crea un archivo `.gitignore` en la raíz del proyecto para excluir archivos innecesarios:

```bash
# Crear .gitignore
cat > .gitignore << 'EOF'
# Build artifacts
build/
dist/
*.bin
*.elf
*.map

# ESP-IDF
sdkconfig.old
dependencies.lock

# IDE
.vscode/
.idea/
*.swp
*.swo
*~
.DS_Store
._.DS_Store

# Python
__pycache__/
*.py[cod]
*$py.class
*.so
.Python
env/
venv/

# Logs
*.log

# Temporary files
*.tmp
*.temp
*.bak

# Managed components (se pueden regenerar)
managed_components/

# Archivos de configuración personal
.devcontainer/
.settings/
.cproject
.project
.clangd
EOF
```

## 🔧 Paso 4: Inicializar Repositorio Local

```bash
# Inicializar repositorio Git
git init

# Agregar todos los archivos (respetando .gitignore)
git add .

# Verificar qué archivos se van a incluir
git status

# Crear el primer commit
git commit -m "Initial commit: ALTIUM48k ESP32 DSP Project"
```

## 🔗 Paso 5: Conectar con GitHub

Reemplaza `TU-USUARIO` con tu nombre de usuario de GitHub:

```bash
# Agregar el repositorio remoto
git remote add origin https://github.com/TU-USUARIO/ALTIUM48k.git

# Verificar que se agregó correctamente
git remote -v

# Subir el código a GitHub (rama main)
git branch -M main
git push -u origin main
```

**Nota**: Te pedirá tu usuario y contraseña de GitHub. Si tienes autenticación de dos factores, necesitarás usar un [Personal Access Token](https://github.com/settings/tokens) en lugar de tu contraseña.

## 🔄 Paso 6: Trabajar con el Repositorio

### Hacer Cambios y Subirlos

```bash
# 1. Ver qué archivos han cambiado
git status

# 2. Agregar archivos específicos
git add main/ALTIUM2025main.c
# O agregar todos los cambios
git add .

# 3. Crear un commit con mensaje descriptivo
git commit -m "Descripción clara de los cambios realizados"

# 4. Subir los cambios a GitHub
git push
```

### Ejemplos de Mensajes de Commit

```bash
git commit -m "Add: Nueva función Grabadefaults() para inicializar presets"
git commit -m "Fix: Corregida configuración de rutas de build en VS Code"
git commit -m "Update: Actualizado manejo de presets en NVS"
git commit -m "Refactor: Optimizado código de comunicación I2C"
```

## 📥 Paso 7: Sincronizar Cambios desde GitHub

Si trabajas desde múltiples computadoras o hay cambios en GitHub:

```bash
# Descargar y fusionar cambios desde GitHub
git pull origin main
```

## 🌿 Paso 8: Trabajar con Ramas (Opcional pero Recomendado)

```bash
# Crear una nueva rama para una característica
git checkout -b feature/nueva-funcionalidad

# Hacer cambios y commits en la rama
git add .
git commit -m "Add: Nueva funcionalidad implementada"

# Subir la rama a GitHub
git push -u origin feature/nueva-funcionalidad

# Volver a la rama main
git checkout main

# Fusionar la rama cuando esté lista
git merge feature/nueva-funcionalidad

# Subir los cambios fusionados
git push
```

## 🔍 Comandos Útiles de Git

```bash
# Ver historial de commits
git log --oneline --graph --all

# Ver cambios antes de hacer commit
git diff

# Deshacer cambios en un archivo (antes de commit)
git checkout -- archivo.c

# Ver estado del repositorio
git status

# Ver ramas locales
git branch

# Ver ramas remotas
git branch -r

# Eliminar archivos del repositorio
git rm archivo.txt
git commit -m "Remove: Eliminado archivo innecesario"
git push
```

## 🏷️ Paso 9: Crear Releases/Versiones

### Método 1: Usando el Script Automático

El proyecto incluye un script `create_release.sh` que lee la versión del código automáticamente:

```bash
# 1. Actualizar VERSION en main/ALTIUM2025main.c (ej: "1.72")
# 2. Hacer commit de los cambios
git add .
git commit -m "chore: Bump version to 1.72"
git push

# 3. Ejecutar el script
./create_release.sh
```

### Método 2: Manual

```bash
# Crear un tag para una versión (usar el número de VERSION del código)
git tag -a v1.71 -m "Release version 1.71 - Descripción de cambios"

# Subir el tag a GitHub
git push origin v1.71

# O subir todos los tags
git push --tags

# Ver todos los tags existentes
git tag

# Ver el tag actual
git describe --tags
```

### Crear Release en GitHub

Después de subir el tag, ve a:
https://github.com/escalator2015/ALTIUM48k/releases/new

1. Selecciona el tag que creaste
2. Agrega un título y descripción
3. Puedes adjuntar archivos binarios (.bin, .elf)
4. Marca como "Pre-release" si es una versión beta
5. Publica el release

## 🔐 Paso 10: Configurar Autenticación SSH (Recomendado)

Para evitar escribir usuario/contraseña cada vez:

```bash
# 1. Generar clave SSH (si no tienes una)
ssh-keygen -t ed25519 -C "tu-email@ejemplo.com"

# 2. Agregar la clave al agente SSH
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# 3. Copiar la clave pública
cat ~/.ssh/id_ed25519.pub

# 4. Ir a GitHub → Settings → SSH and GPG keys → New SSH key
#    Pegar la clave pública

# 5. Cambiar la URL remota a SSH
git remote set-url origin git@github.com:TU-USUARIO/ALTIUM48k.git
```

## 📊 Flujo de Trabajo Diario Recomendado

```bash
# Al iniciar el día
git pull                    # Obtener últimos cambios

# Durante el trabajo
git status                  # Ver qué cambió
git add .                   # Agregar cambios
git commit -m "mensaje"     # Guardar cambios localmente

# Al finalizar el día
git push                    # Subir cambios a GitHub
```

## 🚢 Crear Release Automático con Binario

### Instalar y Configurar GitHub CLI

```bash
# Instalar GitHub CLI (si no está instalado)
sudo apt install gh

# Autenticarse con GitHub
gh auth login
# Seleccionar:
# - GitHub.com
# - HTTPS
# - Yes (authenticate Git with your GitHub credentials)
# - Login with a web browser (recomendado)
# O usar un token de acceso personal
```

### Crear Release con el Script

```bash
# 1. Asegúrate de que todos los cambios estén commiteados
git status

# 2. Actualiza el archivo VERSION con el nuevo número (ej: 1.72)
echo "1.72" > VERSION

# 3. Haz commit del cambio de versión
git add VERSION
git commit -m "chore: Bump version to 1.72"
git push

# 4. Ejecuta el script de release (compilará, creará tag y subirá binario)
./create_release.sh
```

El script automáticamente:
- ✅ Lee la versión del archivo `VERSION`
- ✅ Compila el proyecto si el binario no existe
- ✅ Crea el tag `v1.72` en Git
- ✅ Sube el tag a GitHub
- ✅ Crea la release en GitHub con el binario `ALTIUM2025_v1-72.bin` adjunto

## ⚠️ Solución de Problemas Comunes

### Error: "Updates were rejected"
```bash
git pull origin main --rebase
git push
```

### Deshacer último commit (sin perder cambios)
```bash
git reset --soft HEAD~1
```

### Deshacer último commit (perdiendo cambios)
```bash
git reset --hard HEAD~1
```

### Ver archivos ignorados
```bash
git status --ignored
```

## 📚 Recursos Adicionales

- [Documentación oficial de Git](https://git-scm.com/doc)
- [GitHub Guides](https://guides.github.com/)
- [Git Cheat Sheet](https://education.github.com/git-cheat-sheet-education.pdf)

## ✅ Checklist de Mantenimiento

- [ ] Hacer commits frecuentes con mensajes claros
- [ ] Hacer push al final de cada sesión de trabajo
- [ ] Revisar .gitignore para no subir archivos innecesarios
- [ ] Usar ramas para características nuevas o experimentales
- [ ] Crear tags para versiones importantes
- [ ] Mantener sincronizado con `git pull` antes de empezar a trabajar

---

**Última actualización**: 11 de noviembre de 2025
