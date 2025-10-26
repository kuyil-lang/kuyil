# Kuyil Documentation

Welcome to the comprehensive documentation for Kuyil, a fast compiled scripting language with built-in HTTP/REST support.

## 📖 Getting Started

If you're new to Kuyil, start here:

1. **[Quick Start Guide](guides/quick-start.md)** - Installation and first steps
2. **[Language Reference](guides/language-reference.md)** - Complete syntax reference
3. **[Shared Library Guide](guides/shared-library.md)** - Working with libraries

## 🚀 Core Features

Explore Kuyil's powerful features:

### Web Development
- **[Static File Serving](features/static-files.md)** - Built-in HTTP server with static file support

### Language Features
- **[Enhanced Features](features/enhanced-features.md)** - Inline configuration & module imports
- **[Dynamic Execution](features/dynamic-execution.md)** - Runtime script execution
- **[Backtick Strings](features/backtick-strings.md)** - Advanced string processing

### System Integration
- **[FFI System](features/ffi-system.md)** - Foreign Function Interface for .so libraries
- **[Configuration System](features/config-system.md)** - YAML-based configuration
- **[Library Access](features/library-access.md)** - Programmatic library management
- **[Modular Library System](features/modular-library-system.md)** - Module organization

## ⚙️ Technical Implementation

For developers interested in internals:

### Core Systems
- **[Implementation Summary](implementation/summary.md)** - Overall architecture overview
- **[File Reading System](implementation/file-reading.md)** - File I/O implementation
- **[String Interpolation](implementation/string-interpolation.md)** - String processing

### Advanced Features
- **[FFI Complete Implementation](implementation/ffi-complete.md)** - Complete FFI system
- **[Enhanced Metadata](implementation/enhanced-metadata.md)** - FFI metadata management
- **[Reactive Programming](implementation/reactive-programming.md)** - Green threads & observables
- **[Reactive System Complete](implementation/reactive-complete.md)** - Full reactive implementation

### System Components
- **[Environment Variables](implementation/env-vars.md)** - Environment support
- **[Startup/Shutdown](implementation/startup-shutdown.md)** - Lifecycle management

## 🛠️ Development Tools

Resources for Kuyil development:

- **[Development Helper](development/development-helper.md)** - File watching and auto-reload

### 📊 Project Status & History

Track the project's progress and evolution:

### Achievement Reports
- **[Final Achievement Report](project-status/final-achievement.md)** - Complete FFI system achievement summary
- **[Reactive System Analysis](project-status/reactive-analysis.md)** - Comprehensive reactive programming analysis

## 🗂️ Directory Structure

```
docs/
├── guides/              # User-facing documentation
│   ├── quick-start.md
│   ├── language-reference.md
│   └── shared-library.md
├── features/            # Feature documentation
│   ├── enhanced-features.md
│   ├── ffi-system.md
│   ├── config-system.md
│   ├── static-files.md
│   └── ...
├── implementation/      # Technical implementation details
│   ├── summary.md
│   ├── ffi-complete.md
│   ├── file-reading.md
│   └── ...
├── development/         # Development tools and helpers
│   └── development-helper.md
└── project-status/      # Project history and status
    ├── final-achievement.md
    ├── organization.md
    └── ...
```

## 🤝 Contributing

When contributing to documentation:

1. **Guides** (`docs/guides/`) - User-facing documentation and tutorials
2. **Features** (`docs/features/`) - Feature descriptions and usage
3. **Implementation** (`docs/implementation/`) - Technical details and internals
4. **Development** (`docs/development/`) - Development tools and workflows
5. **Project Status** (`docs/project-status/`) - Historical records and status reports

---

📝 **Note**: This documentation is comprehensive and covers all aspects of Kuyil. Start with the Quick Start Guide if you're new, or browse by category based on your interests.