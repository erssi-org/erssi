# Secure Credential Management for Irssi

## Introduction

This implementation provides secure credential management for Irssi, addressing the plaintext password storage issue (GitHub issue #6). It offers transparent integration with existing Irssi commands while adding optional encryption and flexible storage options.

## Key Features

✅ **Minimal core code changes** - Uses existing CONFIG_REC mechanisms
✅ **Full backward compatibility** - Everything disabled by default
✅ **Runtime-only master password** - Never written to disk
✅ **OpenSSL encryption** - AES-256-CBC with PBKDF2 key derivation
✅ **Flexible storage** - Main config or separate encrypted file
✅ **Automatic sensitive field detection** - Including autosendcmd patterns
✅ **User-controlled backups** - You manage your own security
✅ **Transparent integration** - Uses standard Irssi commands

## Architecture

### Four Independent Configuration Modes

The system provides **two orthogonal settings** that can be combined freely, resulting in **four distinct modes**:

| Mode | Storage Location | Encryption | Use Case |
|------|------------------|------------|----------|
| **1. Config + Plaintext** | Main config file | None | Default, backward compatible |
| **2. Config + Encrypted** | Main config file | AES-256-CBC | Encrypted credentials in main config |
| **3. External + Plaintext** | `.credentials` file | None | Separate file, not encrypted |
| **4. External + Encrypted** | `.credentials` file | AES-256-CBC | **Most secure** - separate encrypted file |

**Settings:**
- Storage: `credential_storage_mode = "config"` or `"external"`
- Encryption: `credential_config_encrypt = ON` or `OFF`

These settings are **completely independent** - you can change storage without affecting encryption, and vice versa.

### Storage Modes

1. **Config Mode** (default)
   - Credentials stored in main Irssi config file (`~/.irssi/config`)
   - Can be plaintext or encrypted
   - Setting: `credential_storage_mode = "config"`

2. **External Mode**
   - Credentials stored in separate file (default: `~/.irssi/.credentials`)
   - Can be plaintext or encrypted
   - Setting: `credential_storage_mode = "external"`
   - Filename configurable via `credential_external_file`

### Encryption

- **Independent of storage mode** - Works for both config and external
- **Setting**: `credential_config_encrypt = ON/OFF`
- **Algorithm**: AES-256-CBC
- **Key derivation**: PBKDF2-HMAC-SHA256 (100,000 iterations)
- **Format**: `salt:iv:encrypted_data` (hex:hex:base64)
- **Master password**: Runtime only, never saved to disk

### Supported Credentials

- Server passwords (`password`)
- SASL username and password (`sasl_username`, `sasl_password`)
- Proxy passwords (`proxy_password`)
- TLS certificate passwords (`tls_pass`)
- OPER passwords (`oper_password`)
- Autosendcmd (when containing NickServ/Q@CServe authentication)

## Installation

### Requirements

- OpenSSL 1.1+ (for encryption support)
- GLib 2.0+
- Meson build system

### Compilation

```bash
# Configure build
meson setup build

# Compile
ninja -C build

# Install (optional)
sudo ninja -C build install
```

### Verification

```bash
# Check compilation
ninja -C build

# Test basic functionality
./build/src/fe-text/irssi --version
```

## Quick Start Guide

### 1. Basic Usage (Plaintext Storage)

```irc
# Start Irssi
irssi

# Check credential system status
/credential status

# Add credentials using standard Irssi commands
/network add -sasl_username myuser -sasl_password mypass freenode
/server add -password serverpass -network freenode irc.freenode.net

# Save configuration
/save

# View stored credentials
/credential list
```

### 2. Enable Encryption

```irc
# Set master password (required for encryption)
/credential passwd MySecretMasterPassword

# Enable encryption
/set credential_config_encrypt ON

# All existing credentials are automatically encrypted
# New credentials added via /network or /server will be encrypted

# Save encrypted configuration
/save
```

### 3. Migrate to External File

```irc
# Set master password if not already set
/credential passwd MySecretMasterPassword

# Enable encryption (optional but recommended)
/set credential_config_encrypt ON

# Migrate to external file
/set credential_storage_mode external

# Credentials are automatically moved to ~/.irssi/.credentials (default)
# You can change filename with: /set credential_external_file <name>
# Main config file no longer contains passwords

# Verify migration
/credential status
/credential list
```

### 4. Unlock Encrypted Credentials

```irc
# After restarting Irssi with encryption enabled
# Credentials are locked until you provide master password

# Unlock with master password
/credential passwd MySecretMasterPassword

# Credentials are now decrypted in memory
# You can connect to networks normally
```

## Commands Reference

### Core Commands

#### `/credential passwd <password>`
Set the master password for encryption/decryption.
- Password is stored in memory only
- Required for encryption/decryption operations
- Password immediately masked in command history (replaced with `*****`)

#### `/credential list`
Display all stored credentials.
- Shows decrypted values if master password is set
- Shows encrypted format if no master password
- Reads directly from storage (config file or .credentials)

#### `/credential status`
Show current credential system status.
- Storage mode (config or external)
- External file path
- Encryption status (ON/OFF)
- Master password status (SET/NOT SET)

### Migration Commands

#### `/credential migrate external`
Migrate credentials from main config to external file.
- Creates `~/.irssi/<filename>` file (using `credential_external_file` setting, default: `.credentials`)
- Removes credentials from main config
- Preserves encryption state
- **WARNING**: Make your own backup with `/save` before migration

#### `/credential migrate config`
Migrate credentials from external file back to main config.
- Moves credentials from external file to config file
- Removes external file
- Preserves encryption state
- **WARNING**: Make your own backup with `/save` before migration

### Encryption Commands

#### `/credential encrypt`
Enable encryption for all credentials.
- Requires master password to be set first
- Automatically encrypts all existing credentials
- Works for both storage modes
- For config mode: requires `/save` to write to disk
- For external mode: automatically saves to .credentials file

#### `/credential decrypt`
Disable encryption (convert to plaintext).
- Requires master password for verification
- Automatically decrypts all credentials
- Works for both storage modes
- For config mode: requires `/save` to write to disk
- For external mode: automatically saves to .credentials file

### Utility Commands

#### `/credential reload`
Reload credentials from external file.
- Only works in external storage mode
- Useful after manual file edits
- Requires master password if encryption is enabled

### Standard Irssi Commands

Use standard Irssi commands to manage credentials:

```irc
# Add network with SASL credentials
/network add -sasl_username user -sasl_password pass NetworkName

# Add server with password
/server add -password serverpass -network NetworkName irc.server.net

# Modify existing network
/network add -sasl_username newuser NetworkName

# Remove network (removes associated credentials)
/network remove NetworkName
```

## Settings

### `credential_storage_mode`
**Values**: `config` | `external`
**Default**: `config`

Controls where credentials are stored:
- `config`: Store in main Irssi configuration file
- `external`: Store in separate `.credentials` file

**Changing this setting automatically triggers migration.**

### `credential_config_encrypt`
**Values**: `ON` | `OFF`
**Default**: `OFF`

Controls encryption for credentials:
- `ON`: Encrypt all credentials (requires master password)
- `OFF`: Store credentials in plaintext

**Works independently of storage mode** - you can have encrypted credentials in either config or external file.

**Changing this setting automatically encrypts/decrypts all credentials.**

### `credential_external_file`
**Values**: filename (without path)
**Default**: `.credentials`

Specifies the filename for external credential storage.

**Important details**:
- Value is **filename only**, NOT a full path (e.g., `.credentials`, not `/path/to/.credentials`)
- File is always created in `~/.irssi/` directory
- Change takes effect immediately via signal handler
- Changing this setting does NOT automatically rename/move existing files
- If you change the filename, you must manually rename the old file:
  ```bash
  mv ~/.irssi/.credentials ~/.irssi/new_name
  ```

**Example**:
```irc
# Check current filename
/set credential_external_file

# Change to different filename
/set credential_external_file .my_secrets
/save

# File will now be ~/.irssi/.my_secrets
```

## Security Considerations

### Security Features

1. **Master Password Protection**
   - Stored in memory only, never written to disk
   - Automatically cleared on exit
   - Password masked in command history (replaced with `*****`)

2. **Strong Encryption**
   - AES-256-CBC symmetric encryption
   - PBKDF2 key derivation (100,000 iterations)
   - Unique salt and IV for each encrypted value

3. **Secure Memory Handling**
   - Passwords cleared with `memset()` before freeing
   - No password logging or debugging output

### Best Practices

1. **Use a strong master password**
   - At least 16 characters
   - Mix of letters, numbers, symbols
   - Not used anywhere else

2. **Use external storage mode**
   - Keeps credentials separate from config
   - Easier to backup separately
   - Can use different permissions (chmod 600)

3. **Enable encryption**
   - Protects against config file theft
   - Defense in depth approach

4. **Manual backups are YOUR responsibility**
   - **ALWAYS** run `/save` before risky operations (migrations, encrypt/decrypt)
   - Backup both `~/.irssi/config` and `~/.irssi/.credentials` manually
   - Test restore procedures regularly
   - Consider version control (git) for config (exclude `.credentials` if plaintext!)
   - Example backup command:
     ```bash
     cp ~/.irssi/config ~/.irssi/config.backup.$(date +%Y%m%d)
     cp ~/.irssi/.credentials ~/.irssi/.credentials.backup.$(date +%Y%m%d)
     ```

5. **File permissions**
   ```bash
   chmod 600 ~/.irssi/config
   chmod 600 ~/.irssi/.credentials
   ```

## File Structure

```
src/core/
├── credential.h              # Public API headers
├── credential.c              # Core credential management (1658 lines)
└── credential-crypto.c       # Cryptographic functions (320 lines)

src/fe-common/core/
├── fe-credential.h           # Command headers
└── fe-credential.c           # Command implementation (454 lines)

Integration points:
├── src/core/core.c           # System initialization
├── src/core/settings.c       # Config read/write hooks
└── src/fe-common/core/fe-common-core.c  # Command initialization
```

## Integration with Irssi Core

### Minimal Code Changes

The implementation integrates non-invasively:

1. **src/core/core.c** (2 lines added)
   ```c
   credential_init();    // In core_init()
   credential_deinit();  // In core_deinit()
   ```

2. **src/core/settings.c** (2 hooks added)
   ```c
   credential_config_write_hook(mainconfig);  // Before save
   credential_config_read_hook(mainconfig);   // After load
   ```

3. **src/fe-common/core/fe-common-core.c** (2 lines added)
   ```c
   fe_credential_init();    // In fe_common_core_init()
   fe_credential_deinit();  // In fe_common_core_deinit()
   ```

### Signal-Based Integration

The system uses Irssi's signal mechanism for transparent integration:

- **"chatnet read"** - Fill credentials when loading network configuration
- **"chatnet saved"** - Capture credentials when saving network
- **"server setup saved"** - Capture server passwords
- **"setup changed"** - React to setting changes
- **"setup reread"** - Notify after credential unlock

This approach avoids modifying core data structures or IRC protocol handling.

## Usage Examples

### Example 1: Basic Setup with Encryption

```irc
# Start Irssi for the first time
irssi

# Set master password
/credential passwd MyVerySecurePassword123

# Enable encryption
/set credential_config_encrypt ON

# Add network credentials
/network add -sasl_username myuser -sasl_password mypass -autosendcmd "msg nickserv identify mypass" Libera

# Add server
/server add -network Libera irc.libera.chat

# Save configuration (credentials are encrypted)
/save

# Check status
/credential status
# Output:
#   Storage mode: config
#   External file: .credentials
#   Config encryption: ON
#   Master password: SET

# View credentials (shows decrypted values)
/credential list
```

### Example 2: Migrate to External File

```irc
# Set master password
/credential passwd MyVerySecurePassword123

# Enable encryption
/set credential_config_encrypt ON

# Migrate to external file
/credential migrate external

# Credentials are now in ~/.irssi/.credentials (default filename)
# Main config no longer contains passwords

# Verify
/credential status
# Output:
#   Storage mode: external
#   External file: .credentials
#   Config encryption: ON
#   Master password: SET
```

### Example 3: Starting Irssi with Encrypted Credentials

```irc
# Start Irssi (credentials are locked)
irssi

# Try to connect (will fail - credentials locked)
/connect Libera

# Unlock credentials with master password
/credential passwd MyVerySecurePassword123

# Now credentials are available
/connect Libera
# Connection succeeds with SASL authentication
```

### Example 4: Disable Encryption

```irc
# Unlock first (required to decrypt)
/credential passwd MyVerySecurePassword123

# Disable encryption
/credential decrypt

# All credentials are now in plaintext
/save
```

## Troubleshooting

### Compilation Issues

**Error: "credential.h: No such file"**
- Verify files are in correct directories
- Check meson.build files include credential sources

**Error: "undefined reference to credential_*"**
- Ensure credential.c is in src/core/meson.build
- Verify core.c calls credential_init()

### Runtime Issues

**"Encryption is enabled but master password not set"**
- Set master password: `/credential passwd <password>`
- Master password is not persistent - must be set each session

**"Failed to decrypt credential"**
- Wrong master password
- Data corruption (check backup)
- Encryption format changed

**"Credentials not loading during /connect"**
- In external mode: Ensure master password is set
- Check `/credential list` to verify credentials exist
- Try `/credential reload` in external mode

### Migration Issues

**"Migration failed - config may be in inconsistent state"**
- **IMPORTANT**: No automatic rollback - restore from YOUR backup!
- Check disk space
- Verify file permissions
- Restore from backup you made before migration:
  ```bash
  cp ~/.irssi/config.backup.YYYYMMDD ~/.irssi/config
  ```
- Check error messages for specific cause

**Credentials disappeared after migration**
- Restore from YOUR backup made before migration
- If no backup exists, credentials may be lost
- **Prevention**: ALWAYS backup before migrations!

## Advanced Topics

### Encryption Format

Encrypted values use the format: `salt:iv:ciphertext`

- **salt**: 32 bytes (64 hex characters) - Random salt for PBKDF2
- **iv**: 16 bytes (32 hex characters) - Random initialization vector for AES
- **ciphertext**: Base64-encoded encrypted data

Example:
```
a1b2c3...64chars...d4e5f6:0f1e2d...32chars...3c4b5a:SGVsbG8gV29ybGQh...
```

### Manual File Editing

**External mode** allows manual editing of the credentials file:

```bash
# Edit credentials file (default: .credentials)
nano ~/.irssi/.credentials

# Or if you changed the filename:
# nano ~/.irssi/your_custom_name

# Format (plaintext):
chatnets = {
  Libera = {
    sasl_username = "myuser";
    sasl_password = "mypass";
  };
};

# Reload in Irssi
/credential reload
```

**Warning**: Manual edits require `/credential reload` and will be encrypted on next save if encryption is enabled.

### Changing External File Name

By default, external credentials are stored in `~/.irssi/.credentials`, but you can change the filename:

**Step 1: Change the setting**
```irc
/set credential_external_file .my_passwords
/save
```

**Step 2: Rename existing file (if you have one)**
```bash
mv ~/.irssi/.credentials ~/.irssi/.my_passwords
```

**Step 3: Reload credentials in Irssi**
```irc
/credential reload
```

**Important notes**:
- The setting accepts **filename only**, not a full path
- File is always in `~/.irssi/` directory
- Changing the setting does NOT automatically move/rename the file
- You must manually rename the old file or Irssi will create a new empty file
- Current filename is shown in `/credential status` output

**Common use cases**:
- Using different filenames for different Irssi profiles
- Organizing credentials separately (e.g., `.work_creds`, `.personal_creds`)
- Security through obscurity (non-standard filename)

### Backup and Restore

**⚠️ NO AUTOMATIC BACKUPS - You Are Responsible!**

This implementation does NOT create automatic backups. Following security best practices (like SSH keys, GPG keys, password managers), **YOU** are responsible for backing up your credentials.

**When to Backup** (CRITICAL):
- ✅ **BEFORE** running `/credential migrate external`
- ✅ **BEFORE** running `/credential migrate config`
- ✅ **BEFORE** enabling encryption (`/set credential_config_encrypt ON`)
- ✅ **BEFORE** disabling encryption (`/set credential_config_encrypt OFF`)
- ✅ **Regularly** as part of your backup routine

**How to Backup**:
```bash
# Create timestamped backups
cp ~/.irssi/config ~/.irssi/config.backup.$(date +%Y%m%d_%H%M%S)
cp ~/.irssi/.credentials ~/.irssi/.credentials.backup.$(date +%Y%m%d_%H%M%S)

# Or use /save in Irssi to create a checkpoint
/save
cp ~/.irssi/config ~/safe-location/irssi-config-backup
```

**Restore from Backup**:
```bash
# Stop Irssi first
cp ~/.irssi/config.backup.YYYYMMDD_HHMMSS ~/.irssi/config
cp ~/.irssi/.credentials.backup.YYYYMMDD_HHMMSS ~/.irssi/.credentials

# Restart Irssi
irssi
```

**Version Control** (Advanced):
```bash
cd ~/.irssi
git init
echo ".credentials" >> .gitignore  # If plaintext!
git add config
git commit -m "Backup config"
```

## Implementation Status

### ✅ Completed Features

- [x] Core credential management infrastructure
- [x] AES-256-CBC encryption with PBKDF2
- [x] Master password handling (runtime only)
- [x] Two storage modes (config/external)
- [x] Automatic encryption on/off toggle
- [x] Migration between storage modes
- [x] Sensitive field auto-detection
- [x] Integration with standard Irssi commands
- [x] Signal-based hooks for transparent operation
- [x] Command history security
- [x] Secure memory handling
- [x] User-controlled backup policy (no automatic backups)
- [x] Full English documentation

### 🔄 Future Enhancements

- [ ] Unit tests suite
- [ ] Interactive password prompts (GUI mode)
- [ ] Master password timeout
- [ ] System keyring integration (e.g., libsecret)
- [ ] Audit logging
- [ ] yubikey/hardware token support

## Compatibility

### Irssi Version Support

- Designed for Irssi 1.5+
- Uses only public Irssi APIs
- No internal structure modifications
- Forward compatible with future versions

### Platform Support

- Linux (tested)
- BSD (should work)
- macOS (should work)
- Windows/Cygwin (untested)

## Contributing

This implementation is ready for review and potential merge into the main Irssi repository.

### Code Style

- Follows Irssi coding conventions
- Uses standard GLib types and functions
- Consistent error handling
- Comprehensive comments

### Testing

Before submitting changes:
```bash
# Compile without errors
ninja -C build

# Test basic commands
./build/src/fe-text/irssi --config=/dev/null --home=/tmp/test

# Verify no memory leaks (if available)
valgrind ./build/src/fe-text/irssi --config=/dev/null --home=/tmp/test
```

## License

GNU General Public License v2 or later
Copyright (C) 2024 Irssi Project

## Contact

For issues, questions, or contributions related to this credential management implementation, please refer to the main Irssi project communication channels.
