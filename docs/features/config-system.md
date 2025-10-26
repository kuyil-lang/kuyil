# Kuyil Configuration System Guide

## Overview

The Kuyil Configuration System provides comprehensive YAML-based application configuration with multi-environment support, hierarchical config loading, and runtime configuration management.

## Key Features

- **YAML Configuration**: Native YAML parsing and loading
- **Multi-Environment Support**: Site and region-based configuration hierarchies
- **Hierarchical Loading**: Cascading configuration from base to environment-specific
- **Runtime Configuration**: Dynamic configuration access and modification
- **Nested File Support**: Configuration files can reference and include other files
- **Type Safety**: Automatic type conversion for strings, numbers, booleans, and objects
- **Path Notation**: Dot-notation access to nested configuration values
- **Configuration Merging**: Deep merging of configuration objects
- **Environment Variables**: Integration with environment variable system

## Configuration Hierarchy

The system loads configuration files in the following order (later files override earlier ones):

1. **Base Configuration**
   - `config/application.yml` - Main application settings
   - `config/database.yml` - Database configuration
   - `config/logging.yml` - Logging settings
   - `config/features.yml` - Feature flags

2. **Site-Specific Configuration**
   - `config/sites/us.yml` - US site settings
   - `config/sites/eu.yml` - EU site settings

3. **Region-Specific Configuration**
   - `config/regions/useast1.yml` - US East 1 region
   - `config/regions/useast2.yml` - US East 2 region

4. **Environment-Specific Configuration**
   - `config/environments/us_useast1.yml` - Combined US + US East 1
   - `config/environments/us_useast2.yml` - Combined US + US East 2

5. **Local Overrides**
   - `config/local.yml` - Local development overrides
   - `.kylnv.yml` - Environment-specific overrides

## API Reference

### Core Functions

#### `init_config(site, region)`
Initialize the configuration manager for a specific site and region.

```kuyil
// Initialize for US East 1
let config = init_config("us", "useast1")

// Initialize for EU (no specific region)
let config = init_config("eu", "default")
```

#### `get_config(key_path, default_value)`
Get a configuration value using dot notation.

```kuyil
// Get simple value
let port = get_config("server.port", 8080)

// Get nested value
let db_host = get_config("database.connections.primary.host", "localhost")

// Get with default
let timeout = get_config("cache.timeout", 300)
```

#### `set_config(key_path, value)`
Set a configuration value at runtime.

```kuyil
// Set simple value
set_config("logging.level", "debug")

// Set nested value
set_config("cache.redis.port", 6380)
```

#### `reload_config()`
Reload all configuration files.

```kuyil
reload_config()
log_info("Configuration reloaded")
```

#### `get_environment_info()`
Get information about the current environment.

```kuyil
let env = get_environment_info()
log_info("Site: " + env.site)
log_info("Region: " + env.region)
log_info("Files loaded: " + env.loaded_files.length)
```

### Low-Level Functions

#### `load_yaml(filepath)`
Load a YAML file and return parsed object.

```kuyil
let config = load_yaml("config/application.yml")
if config {
    log_info("Configuration loaded successfully")
}
```

#### `merge_config(base, override)`
Merge two configuration objects.

```kuyil
let base = load_yaml("config/base.yml")
let override = load_yaml("config/override.yml")
let merged = merge_config(base, override)
```

#### `config_get(config, key_path)`
Get value from configuration object using path.

```kuyil
let config = load_yaml("config/app.yml")
let value = config_get(config, "database.host")
```

## Configuration Examples

### Basic Application Configuration

```yaml
# config/application.yml
application:
  name: "Kuyil Web Service"
  version: "1.0.0"
  description: "High-performance web service"

server:
  host: "0.0.0.0"
  port: 3000
  timeout: 30
  max_connections: 1000

logging:
  level: "info"
  format: "json"
  output: "stdout"
```

### Site-Specific Configuration

```yaml
# config/sites/us.yml
site:
  name: "us"
  display_name: "United States"
  timezone: "America/New_York"
  currency: "USD"

compliance:
  gdpr_enabled: false
  ccpa_enabled: true

apis:
  payment_gateway: "stripe"
```

```yaml
# config/sites/eu.yml
site:
  name: "eu"
  display_name: "European Union"
  timezone: "Europe/London"
  currency: "EUR"

compliance:
  gdpr_enabled: true
  ccpa_enabled: false

apis:
  payment_gateway: "adyen"
```

### Region-Specific Configuration

```yaml
# config/regions/useast1.yml
region:
  name: "useast1"
  display_name: "US East (N. Virginia)"
  aws_region: "us-east-1"

server:
  port: 3001

database:
  host: "us-east-1-db.amazonaws.com"
  
performance:
  auto_scaling: true
  min_instances: 2
  max_instances: 10
```

### Environment-Specific Configuration

```yaml
# config/environments/us_useast1.yml
environment:
  name: "us_useast1"
  tier: "production"

logging:
  level: "warn"
  
security:
  rate_limit_per_minute: 1000
  
monitoring:
  alerts_enabled: true
  
backup:
  enabled: true
  schedule: "0 1 * * *"
```

## Usage Examples

### Basic Configuration Loading

```kuyil
// Load configuration for US East 1
log_info("Initializing configuration...")
let config = init_config("us", "useast1")

// Access configuration values
let app_name = get_config("application.name", "Unknown App")
let port = get_config("server.port", 8080)
let db_host = get_config("database.host", "localhost")

log_info("Application: " + app_name)
log_info("Port: " + port)
log_info("Database: " + db_host)
```

### Multi-Environment Comparison

```kuyil
// Compare different environments
log_info("Comparing environments...")

// US East 1
init_config("us", "useast1")
let us_east1_port = get_config("server.port", 3000)
let us_east1_workers = get_config("server.worker_processes", 1)

// US East 2
init_config("us", "useast2")
let us_east2_port = get_config("server.port", 3000)
let us_east2_workers = get_config("server.worker_processes", 1)

log_info("US East 1 - Port: " + us_east1_port + ", Workers: " + us_east1_workers)
log_info("US East 2 - Port: " + us_east2_port + ", Workers: " + us_east2_workers)
```

### Runtime Configuration Updates

```kuyil
// Modify configuration at runtime
init_config("us", "useast1")

log_info("Original log level: " + get_config("logging.level", "info"))

// Update configuration
set_config("logging.level", "debug")
set_config("features.new_feature", true)

log_info("Updated log level: " + get_config("logging.level", "info"))
log_info("New feature: " + get_config("features.new_feature", false))
```

### HTTP Server with Configuration

```kuyil
// Start HTTP server with configuration
init_config("us", "useast1")

let host = get_config("server.host", "0.0.0.0")
let port = get_config("server.port", 8080)
let timeout = get_config("server.timeout", 30)

log_info("Starting server on " + host + ":" + port)

http_start_server(host, port, fn(request, response) {
    let config_info = {
        "site": get_config("site.name", "unknown"),
        "region": get_config("region.name", "unknown"),
        "environment": get_config("environment.name", "development"),
        "version": get_config("application.version", "1.0.0")
    }
    
    http_json(response, config_info)
})
```

### Configuration Validation

```kuyil
// Validate required configuration
init_config("us", "useast1")

let required_configs = [
    "application.name",
    "server.host",
    "server.port",
    "database.host"
]

let missing_configs = []

for config_key in required_configs {
    let value = get_config(config_key)
    if !value {
        missing_configs.push(config_key)
    }
}

if missing_configs.length > 0 {
    log_error("Missing required configurations:")
    for missing in missing_configs {
        log_error("  - " + missing)
    }
} else {
    log_info("All required configurations present")
}
```

## Error Handling

The configuration system provides robust error handling:

```kuyil
// Handle missing files gracefully
let config = load_yaml("config/nonexistent.yml")
if !config {
    log_warning("Config file not found, using defaults")
}

// Use defaults for missing keys
let port = get_config("server.port", 8080)  // Returns 8080 if not found
let missing = get_config("non.kylxistent.key")  // Returns nil if not found

// Validate configuration before use
if get_config("database.host") {
    log_info("Database host configured: " + get_config("database.host"))
} else {
    log_error("Database host not configured!")
}
```

## Best Practices

### 1. Environment Separation
```kuyil
// Use environment variables to select configuration
let site = getenv("KUYIL_SITE") || "us"
let region = getenv("KUYIL_REGION") || "default"
init_config(site, region)
```

### 2. Configuration Validation
```kuyil
// Validate critical configuration on startup
fn validate_config() {
    let critical_keys = [
        "database.host",
        "application.name",
        "server.port"
    ]
    
    for key in critical_keys {
        if !get_config(key) {
            log_fatal("Critical configuration missing: " + key)
            return false
        }
    }
    return true
}

if !validate_config() {
    log_fatal("Configuration validation failed")
    exit(1)
}
```

### 3. Secrets Management
```yaml
# Don't put secrets in config files
security:
  jwt_secret_from_env: true  # Read from environment
  
database:
  password_from_env: "DB_PASSWORD"  # Environment variable name
```

```kuyil
// Read secrets from environment
let jwt_secret = getenv("JWT_SECRET")
if !jwt_secret {
    log_fatal("JWT_SECRET environment variable required")
    exit(1)
}
```

### 4. Configuration Caching
```kuyil
// Cache frequently accessed configuration
let cached_config = {}

fn get_cached_config(key, default_value) {
    if cached_config[key] {
        return cached_config[key]
    }
    
    let value = get_config(key, default_value)
    cached_config[key] = value
    return value
}
```

## Command Line Usage

```bash
# Run with specific environment
KUYIL_SITE=us KUYIL_REGION=useast1 ./kuyil app.kyl

# Run configuration demo
make config-demo

# Test specific environments
make config-us-east1
make config-us-east2
make config-eu

# Run with configuration examples
make config-examples
```

## File Structure

```
examples/
├── config/
│   ├── application.yml          # Base application config
│   ├── database.yml             # Database configuration
│   ├── sites/
│   │   ├── us.yml              # US site configuration
│   │   └── eu.yml              # EU site configuration
│   ├── regions/
│   │   ├── useast1.yml         # US East 1 region
│   │   └── useast2.yml         # US East 2 region
│   └── environments/
│       ├── us_useast1.yml      # US + US East 1 combined
│       └── us_useast2.yml      # US + US East 2 combined
├── config_manager.kyl            # Configuration manager library
└── config_demo.kyl               # Comprehensive demo
```

This configuration system enables sophisticated multi-environment application deployment while maintaining clean separation of concerns and easy management of environment-specific settings.