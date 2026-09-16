# Dissonance Development Guide

## Architecture Overview

This project has two repositories:

- **core** (`C:\Users\luca\CLionProjects\core`) - C++ audio processing engine (Node.js native addon)
- **ui** (`C:\Users\luca\OneDrive\Documents\Dissonance\ui`) - Electron app (JavaScript UI)

**Data Flow:**
```
Electron UI → IPC → C++ Addon → Audio Processing → IPC → Electron UI
```

### FFT implementation (early research)
- Current implementation uses a simple O(N^2) DFT/iDFT for correctness and zero dependencies.
- Goal: validate the processing chain (window → FFT → modify → iFFT) before choosing a high-performance library.
- Upgrade path: swap in a faster library (KissFFT/pffft/FFTW) when performance requirements are defined.
- Tests: JavaScript (`test-fft.js`) and C++ (`tests/audio/fft_processor_test.cpp`) cover impulse/round-trip correctness.

## Quick Start Workflow

### 1. Making Changes to C++ Code

Edit your audio processing logic in `src/addon.cpp` in the `processWavFile()` function.

### 2. Build & Deploy

Run these commands to rebuild and test your changes:

```bash
# Step 1: Build the C++ addon
cd C:\Users\luca\CLionProjects\core
npm run build

# Step 2: Copy the built addon to the UI
cd C:\Users\luca\OneDrive\Documents\Dissonance\ui
npm run copy-core-addon

# Step 3: Run the UI to test
npm run dev
```

### 3. Testing

- Open a WAV file in the UI
- Click "Process"
- Look for `[C++]` logs in the terminal to confirm your code is running
- Check the output file path shown in the UI

## One-Command Workflow

To make life easier, add this script to your workflow:

### Option 1: PowerShell Script

Create `C:\Users\luca\CLionProjects\core\rebuild-and-test.ps1`:

```powershell
# Build core
cd C:\Users\luca\CLionProjects\core
Write-Host "Building C++ addon..." -ForegroundColor Cyan
npm run build

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# Copy to UI
cd C:\Users\luca\OneDrive\Documents\Dissonance\ui
Write-Host "Copying addon to UI..." -ForegroundColor Cyan
npm run copy-core-addon

if ($LASTEXITCODE -ne 0) {
    Write-Host "Copy failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Success! Now run 'npm run dev' in the UI folder to test." -ForegroundColor Green
```

Run it with:
```bash
powershell -ExecutionPolicy Bypass -File C:\Users\luca\CLionProjects\core\rebuild-and-test.ps1
```

### Option 2: NPM Script (Recommended)

Add to `core/package.json`:

```json
{
  "scripts": {
    "build": "node-gyp configure build",
    "deploy": "npm run build && cd ../../../OneDrive/Documents/Dissonance/ui && npm run copy-core-addon"
  }
}
```

Then just run:
```bash
cd C:\Users\luca\CLionProjects\core
npm run deploy
```

## File Structure

### Core (C++)
```
core/
├── src/
│   └── addon.cpp          ← Your C++ audio processing code
├── build/
│   └── Release/
│       └── dissonance_core.node  ← Built addon
├── package.json
└── binding.gyp            ← Build configuration
```

### UI (Electron)
```
ui/
├── ipcHandlers/
│   └── dissonanceCore.js  ← Bridge between UI and C++ addon
├── build/
│   └── Release/
│       └── dissonance_core.node  ← Copied addon (used by UI)
├── renderer.js            ← UI logic
├── preload.js             ← IPC API
└── main.js                ← Electron main process
```

## Key Functions

### C++ (src/addon.cpp)

**`processWavFile(inputPath, outputPath)`**
- Implement your audio processing here
- Currently just copies the file
- Add your WAV manipulation logic

**`Process(CallbackInfo)`**
- Node.js binding function
- Handles path parsing and error handling
- Returns `{ processedPath: "..." }`

### JavaScript (ui/ipcHandlers/dissonanceCore.js)

**`core:process` IPC handler**
- Receives file path from UI
- Calls `coreAddon.process(filePath, options)`
- Returns result to UI

## Debugging

### Check if C++ code is running

Look for `[C++]` logs in the terminal when processing:
```
[C++] Processing started: C:\path\to\file.wav
[C++] Processing complete: C:\path\to\file-processed.wav
```

### Test the addon directly

```bash
cd C:\Users\luca\CLionProjects\core
node -e "const addon = require('./build/Release/dissonance_core.node'); console.log(addon.hello());"
```

### Common Issues

**Build fails:** Delete `build` folder and rebuild
```bash
cd C:\Users\luca\CLionProjects\core
rm -r build -Force
npm run build
```

**No [C++] logs:** The addon wasn't copied or UI needs restart
```bash
cd C:\Users\luca\OneDrive\Documents\Dissonance\ui
npm run copy-core-addon
# Restart the UI (Ctrl+C and npm run dev)
```

**Wrong addon loaded:** Check the startup logs - should say "Loaded addon from local build path"

## Development Tips

1. **Always rebuild after C++ changes** - JavaScript changes are instant, C++ requires rebuild
2. **Restart the UI after copying addon** - Node.js caches native modules
3. **Use the [C++] logs** - Easy way to verify your code is running
4. **Test incrementally** - Make small changes and test often

## Next Steps

Replace the placeholder code in `processWavFile()` with your actual audio processing:

```cpp
bool processWavFile(const std::string& inputPath, const std::string& outputPath) {
    std::cout << "[C++] Processing started: " << inputPath << std::endl;
    
    // TODO: Your audio processing logic here
    // 1. Read WAV file
    // 2. Apply audio effects/transformations
    // 3. Write processed WAV file
    
    std::cout << "[C++] Processing complete: " << outputPath << std::endl;
    return true;
}
```

Good luck! 🚀
