# Irssi Credential Management - HOWTO Guide

## Table of Contents

1. [Getting Started](#getting-started)
2. [Common Use Cases](#common-use-cases)
3. [Step-by-Step Guides](#step-by-step-guides)
4. [Best Practices](#best-practices)
5. [Troubleshooting Common Problems](#troubleshooting-common-problems)
6. [FAQ](#frequently-asked-questions)

## Getting Started

### What is Credential Management?

Irssi's credential management system provides:
- **Secure storage** for IRC passwords, SASL credentials, and other sensitive data
- **Optional encryption** using AES-256 with a master password
- **Flexible storage** in either your main config or a separate file
- **Transparent operation** - works with standard Irssi commands

### Four Configuration Modes

The system has **two independent settings** that combine to create **four modes**:

| # | Storage | Encryption | Description |
|---|---------|------------|-------------|
| 1 | Config file | OFF | **Default** - plaintext in `~/.irssi/config` (backward compatible) |
| 2 | Config file | ON | Encrypted credentials in `~/.irssi/config` |
| 3 | External file | OFF | Plaintext in `~/.irssi/.credentials` (separate from config) |
| 4 | External file | ON | **Most secure** - encrypted in `~/.irssi/.credentials` |

**You choose:**
- Where to store: `credential_storage_mode = "config"` or `"external"`
- Whether to encrypt: `credential_config_encrypt = ON` or `OFF`

These settings work **independently** - you can change one without affecting the other.

### Do I Need This?

You should use credential management if you:
- Store SASL passwords for IRC networks
- Use server passwords
- Want to encrypt sensitive data in your config
- Need to separate credentials from your main configuration
- Share your Irssi config via dotfiles but want to keep passwords private

### Basic Concept

The system works in the background. You use normal Irssi commands like `/network add` and `/server add`, and the credential system automatically:
1. Detects sensitive fields (passwords, SASL credentials)
2. Stores them securely
3. Optionally encrypts them
4. Provides them when needed for connections

## Common Use Cases

### Use Case 1: I Want to Start Using Encryption

**Before**: Your `~/.irssi/config` contains plaintext passwords:
```
chatnets = {
  Libera = {
    sasl_username = "myuser";
    sasl_password = "myplaintextpassword";  # ← Visible in file!
  };
};
```

**After**: Passwords are encrypted:
```
chatnets = {
  Libera = {
    sasl_username = "a1b2c3d4...encrypted...";
    sasl_password = "e5f6g7h8...encrypted...";  # ← Protected!
  };
};
```

**How**: [See Guide #1](#guide-1-enable-encryption-for-existing-credentials)

---

### Use Case 2: I Want Credentials in a Separate File

**Before**: Everything in `~/.irssi/config`

**After**:
- `~/.irssi/config` - Your settings and preferences
- `~/.irssi/.credentials` - All passwords (separate, can use different permissions)

**Why**:
- Easier to share your config publicly
- Can backup credentials separately
- Stricter file permissions on credentials

**How**: [See Guide #2](#guide-2-move-credentials-to-external-file)

---

### Use Case 3: I Share My Dotfiles on GitHub

**Problem**: You want to version control your Irssi config but can't include passwords.

**Solution**: Use external storage mode
1. Keep `~/.irssi/config` in git
2. Keep `~/.irssi/.credentials` out of git (add to `.gitignore`)
3. Credentials are automatically managed separately

**How**: [See Guide #2](#guide-2-move-credentials-to-external-file)

---

### Use Case 4: Multiple Machines with Same Config

**Scenario**: You use Irssi on your laptop and VPS.

**Solution**:
- Sync `~/.irssi/config` between machines (same settings)
- Keep `~/.irssi/.credentials` different per machine (different passwords/keys)

**How**: [See Guide #3](#guide-3-setup-for-multiple-machines)

## Step-by-Step Guides

### Guide #1: Enable Encryption for Existing Credentials

**Prerequisites**: You already have networks configured in Irssi.

**Steps**:

1. **Start Irssi**
   ```
   irssi
   ```

2. **Check what you have**
   ```irc
   /credential status
   ```
   You should see:
   ```
   Storage mode: config
   Config encryption: OFF
   Master password: NOT SET
   ```

3. **Set a strong master password**
   ```irc
   /credential passwd YourVeryStrongMasterPassword123!
   ```

   ⚠️ **Important**:
   - Remember this password! It's not stored anywhere.
   - You'll need to enter it each time you start Irssi.
   - If you forget it, you can't decrypt your credentials.

4. **Enable encryption**
   ```irc
   /set credential_config_encrypt ON
   ```

   You'll see a message that credentials are being encrypted.

5. **Save your config**
   ```irc
   /save
   ```

6. **Verify it worked**
   ```irc
   /credential list
   ```

   You should see your credentials listed (in decrypted form because you have the master password set).

7. **Test by restarting**
   ```irc
   /quit
   ```

   Start Irssi again:
   ```
   irssi
   ```

   Try to connect - it will fail because credentials are locked:
   ```irc
   /connect Libera
   ```

   Unlock with your master password:
   ```irc
   /credential passwd YourVeryStrongMasterPassword123!
   ```

   Now connect successfully:
   ```irc
   /connect Libera
   ```

**Done!** Your credentials are now encrypted.

---

### Guide #2: Move Credentials to External File

**Why**: Separate credentials from config, easier to manage permissions.

**Steps**:

1. **Set master password** (recommended but optional)
   ```irc
   /credential passwd YourMasterPassword
   ```

2. **Enable encryption** (optional but recommended)
   ```irc
   /set credential_config_encrypt ON
   ```

3. **⚠️ BACKUP FIRST!**
   ```irc
   /save
   ```
   Then exit Irssi and:
   ```bash
   cp ~/.irssi/config ~/.irssi/config.backup.$(date +%Y%m%d)
   ```
   Restart Irssi.

4. **Migrate to external file**
   ```irc
   /credential migrate external
   ```

   This will:
   - Create `~/.irssi/.credentials`
   - Move all passwords there
   - Remove them from main config
   - **NO automatic backup** - you already made one!

5. **Verify migration**
   ```irc
   /credential status
   ```

   Should show:
   ```
   Storage mode: external
   External file: .credentials
   ```

6. **Check the files**

   Exit Irssi and check:
   ```bash
   # Your main config no longer has passwords
   grep -i password ~/.irssi/config

   # Passwords are now in .credentials
   ls -la ~/.irssi/.credentials
   ```

7. **Set appropriate permissions**
   ```bash
   chmod 600 ~/.irssi/.credentials
   ```

**Done!** Credentials are now in a separate file.

---

### Guide #3: Setup for Multiple Machines

**Scenario**: You want the same config on laptop and VPS, but different credentials.

**On your main machine**:

1. **Use external storage**
   ```irc
   /credential migrate external
   ```

2. **Setup git for your config** (optional)
   ```bash
   cd ~/.irssi
   git init

   # Ignore credentials file
   echo ".credentials" >> .gitignore
   echo "*.backup.*" >> .gitignore

   # Add your config
   git add config .gitignore
   git commit -m "Initial Irssi config"
   ```

**On other machines**:

1. **Clone/copy your config** (without credentials)
   ```bash
   cd ~/.irssi
   # Copy or git clone your config
   git clone your-repo .
   ```

2. **Create credentials on this machine**
   ```
   irssi
   ```

   ```irc
   # Set storage mode
   /set credential_storage_mode external

   # Add this machine's credentials
   /network add -sasl_username user -sasl_password thispass Libera

   # Save
   /save
   ```

**Done!** Same config, different credentials per machine.

---

### Guide #4: Add New Network with SASL

**Scenario**: You want to add a new IRC network with SASL authentication.

**Steps**:

1. **Optional: Set master password if using encryption**
   ```irc
   /credential passwd YourMasterPassword
   ```

2. **Add the network with credentials**
   ```irc
   /network add -sasl_username yourname -sasl_password yourpass Libera
   ```

   The credential system automatically detects and stores these.

3. **Add a server for this network**
   ```irc
   /server add -network Libera irc.libera.chat 6697
   ```

4. **Save**
   ```irc
   /save
   ```

5. **Connect**
   ```irc
   /connect Libera
   ```

**Done!** New network added with secure credential storage.

---

### Guide #5: Disable Encryption (Go Back to Plaintext)

**Warning**: This will decrypt all your credentials to plaintext.

**Steps**:

1. **Unlock first** (need master password to decrypt)
   ```irc
   /credential passwd YourMasterPassword
   ```

2. **Disable encryption**
   ```irc
   /credential decrypt
   ```

   All credentials are converted to plaintext.

3. **Save**
   ```irc
   /save
   ```

4. **Verify**
   ```irc
   /credential status
   ```

   Should show:
   ```
   Config encryption: OFF
   ```

**Done!** Credentials are now in plaintext (not recommended for production).

## Best Practices

### Password Management

1. **Use a strong master password**
   - Minimum 16 characters
   - Mix of uppercase, lowercase, numbers, symbols
   - Not used for anything else
   - Example: `Tr0ub4dour&3-Irssi-M4ster!`

2. **Don't share your master password**
   - It's not stored anywhere
   - Only you should know it
   - Each user should have their own

3. **Consider a password manager**
   - Store your master password in KeePassXC, 1Password, etc.
   - More secure than writing it down
   - Can generate strong passwords

### Storage Strategy

**Recommended Setup**:
```
Storage mode: external
Encryption: ON
Master password: Strong and unique
```

**Why**:
- Credentials separate from config
- Encrypted for security
- Can share config safely
- Better permission management

### File Permissions

Always set strict permissions:

```bash
# Main config
chmod 600 ~/.irssi/config

# Credentials file (if using external mode)
chmod 600 ~/.irssi/.credentials

# Verify
ls -la ~/.irssi/
```

Should show:
```
-rw------- 1 user user  ... config
-rw------- 1 user user  ... .credentials
```

### Backup Strategy

**Before making changes**:

```bash
# Manual backup
cp ~/.irssi/config ~/.irssi/config.backup.$(date +%Y%m%d)
cp ~/.irssi/.credentials ~/.irssi/.credentials.backup.$(date +%Y%m%d)
```

**Regular backups**:

```bash
# Add to your backup script
tar czf ~/backups/irssi-$(date +%Y%m%d).tar.gz \
  ~/.irssi/config \
  ~/.irssi/.credentials
```

**⚠️ IMPORTANT**: There are NO automatic backups! You MUST create backups manually before:
- Migrations (`/credential migrate`)
- Enabling/disabling encryption (`/set credential_config_encrypt`)
- Any risky operation

### Session Management

**Starting Irssi with encryption**:

1. Start Irssi
2. Unlock credentials: `/credential passwd YourPassword`
3. Connect to networks: `/connect NetworkName`

**Pro tip**: Create a startup script:
```bash
#!/bin/bash
# ~/start-irssi.sh

echo "Starting Irssi..."
irssi

# After exit, clear terminal
clear
echo "Irssi closed."
```

### Sharing Config Publicly

If you want to share your Irssi config (e.g., on GitHub):

1. **Use external storage mode**
2. **Add to `.gitignore`**:
   ```gitignore
   .credentials
   *.backup.*
   config.autosave
   ```

3. **Verify before committing**:
   ```bash
   grep -i password config  # Should return nothing
   ```

## Troubleshooting Common Problems

### Problem: "Encryption is enabled but master password not set"

**Symptom**: Can't connect to networks after restart.

**Cause**: Master password is runtime-only and must be set each session.

**Solution**:
```irc
/credential passwd YourMasterPassword
```

---

### Problem: "Failed to decrypt credential"

**Symptom**: Error message when trying to unlock or connect.

**Possible Causes**:
1. Wrong master password
2. Data corruption
3. Changed encryption format

**Solutions**:

1. **Try password again** (case-sensitive!)
   ```irc
   /credential passwd YourMasterPassword
   ```

2. **Check if data is corrupted**
   ```bash
   # Look at the file
   cat ~/.irssi/config  # or .credentials
   ```

   Encrypted data should look like:
   ```
   sasl_password = "a1b2c3...64chars...:f4e5d6...32chars...:SGVs...base64...";
   ```

3. **Restore from YOUR backup**
   ```bash
   # Find backups YOU created
   ls -la ~/.irssi/config.backup.*

   # Restore from YOUR backup
   cp ~/.irssi/config.backup.20241008_120000 ~/.irssi/config
   ```

---

### Problem: Credentials not loading during `/connect`

**Symptom**: Connection fails with "SASL auth failed" or similar.

**Debugging Steps**:

1. **Check credential status**
   ```irc
   /credential status
   ```

2. **List credentials**
   ```irc
   /credential list
   ```

   Verify your network is listed.

3. **If using external mode, reload**
   ```irc
   /credential reload
   ```

4. **Check network configuration**
   ```irc
   /network
   ```

   Verify network exists and has correct settings.

5. **Try manual connection with password**
   ```irc
   /connect -sasl_username user -sasl_password pass irc.network.net
   ```

---

### Problem: Can't migrate to external file

**Symptom**: "/credential migrate external" fails.

**Possible Causes**:
1. Disk full
2. Permission issues
3. .credentials file already exists

**Solutions**:

1. **Check disk space**
   ```bash
   df -h ~/.irssi
   ```

2. **Check permissions**
   ```bash
   ls -la ~/.irssi/
   touch ~/.irssi/test && rm ~/.irssi/test  # Test write
   ```

3. **Remove old .credentials if exists**
   ```bash
   mv ~/.irssi/.credentials ~/.irssi/.credentials.old
   ```

   Then try migration again.

---

### Problem: Master password not working

**Symptom**: Correct password rejected.

**Debugging**:

1. **Check encryption status**
   ```irc
   /credential status
   ```

   If encryption is OFF, you don't need a password.

2. **Check for typos**
   - Master password is case-sensitive
   - No extra spaces
   - Special characters must be exact

3. **Try in a text file first**
   ```bash
   echo "YourMasterPassword" > /tmp/testpass
   # Verify it looks right
   cat /tmp/testpass
   rm /tmp/testpass
   ```

---

### Problem: Lost master password

**Symptom**: Can't remember your master password.

**Bad news**: There's no password recovery. The password is never stored.

**Options**:

1. **If you have plaintext backup**
   ```bash
   # Restore old config
   cp ~/.irssi/config.backup.before-encryption ~/.irssi/config
   ```

2. **Reset and start over**
   ```irc
   # Disable encryption
   /set credential_config_encrypt OFF

   # Will warn about locked credentials - ignore

   # Manually re-enter all credentials
   /network add -sasl_username user -sasl_password pass Network

   # Save
   /save
   ```

**Prevention**: Write your master password down securely (password manager, safe, etc.)

## Frequently Asked Questions

### Q: Is my master password stored anywhere?

**A**: No. The master password exists only in RAM while Irssi is running. It's never written to disk, never logged, and cleared when you exit.

---

### Q: What happens if I forget my master password?

**A**: Unfortunately, you cannot recover it. You'll need to:
1. Restore from an unencrypted backup, or
2. Disable encryption and re-enter your credentials manually

---

### Q: Can I use different passwords for different networks?

**A**: The master password unlocks *all* encrypted credentials. Each network has its own SASL password, but they're all encrypted with the same master password.

---

### Q: Does encryption slow down Irssi?

**A**: No noticeable performance impact. Encryption/decryption happens:
- Once when loading credentials (startup)
- Once when saving credentials (/save)
- Not during normal IRC operation

---

### Q: Can I use this with irssi-proxy or ZNC?

**A**: Yes. The credential system is transparent to IRC protocol.
- Works with irssi-proxy
- Works with ZNC
- Works with any IRC bouncer

---

### Q: What encryption algorithm is used?

**A**:
- **Cipher**: AES-256-CBC (industry standard)
- **Key derivation**: PBKDF2-HMAC-SHA256 with 100,000 iterations
- **IV**: Random 16 bytes per encrypted value
- **Salt**: Random 32 bytes per encrypted value

This is the same encryption used by many password managers.

---

### Q: Is it safe to commit my config to git?

**A**:
- With **external mode**: Yes, if you exclude `.credentials` from git
- With **encryption ON**: Probably yes, but better to use external mode
- With **plaintext**: No, passwords are visible in the file

Recommended `.gitignore`:
```
.credentials
*.backup.*
config.autosave
```

---

### Q: Can I share one .credentials file across multiple machines?

**A**: You can, but it's not recommended:
- Same master password everywhere
- Syncing conflicts if edited on multiple machines
- Better: Use separate credentials per machine

---

### Q: Can I change the name of the .credentials file?

**A**: Yes! The filename `.credentials` is just the default. You can change it:

```irc
# Change filename
/set credential_external_file .my_passwords
/save
```

**Important**: This only changes the setting. You must manually rename the existing file:
```bash
mv ~/.irssi/.credentials ~/.irssi/.my_passwords
```

Then reload in Irssi:
```irc
/credential reload
```

**Notes**:
- Setting accepts **filename only** (not full path)
- File is always in `~/.irssi/` directory
- Useful for multiple Irssi profiles or organization
- Check current filename: `/credential status`

---

### Q: How do I change my master password?

**A**:
1. Unlock with old password: `/credential passwd OldPassword`
2. Decrypt all: `/credential decrypt`
3. Set new password: `/credential passwd NewPassword`
4. Re-encrypt: `/credential encrypt`
5. Save: `/save`

---

### Q: Does this work with Irssi scripts?

**A**: Yes. Scripts that use standard Irssi commands (`/network`, `/server`, etc.) work normally. The credential system is transparent.

---

### Q: Can I inspect what's encrypted?

**A**: Yes:
```irc
# If master password is set
/credential list

# Shows decrypted values
```

Or look at the file:
```bash
# External mode
cat ~/.irssi/.credentials

# Config mode
grep -A 5 "chatnets" ~/.irssi/config
```

Encrypted values look like: `a1b2c3...:f4e5...:SGVs...==`

---

### Q: What if OpenSSL is not available?

**A**: The system compiles and works without encryption:
- Storage modes work normally
- Migration works normally
- Encryption features disabled
- Warning displayed at startup

---

### Q: Is this officially part of Irssi?

**A**: This implementation is a proposal/patch for the main Irssi project. Check the official Irssi repository to see if it's been merged.

---

### Q: Where can I get help?

**A**:
1. Check this HOWTO
2. Read CREDENTIAL_MANAGEMENT_README.md
3. Check Irssi documentation
4. Ask in #irssi on Libera.Chat
5. File an issue on GitHub

---

## Quick Reference Card

### Common Commands

```irc
/credential passwd <password>     # Set/unlock master password
/credential status                # Show current status
/credential list                  # List all credentials
/credential migrate external      # Move to external file
/credential migrate config        # Move to main config
/credential encrypt               # Enable encryption
/credential decrypt               # Disable encryption
/credential reload                # Reload external file

/network add -sasl_username <u> -sasl_password <p> <name>
/server add -password <p> -network <name> <address>
```

### Settings

```irc
/set credential_storage_mode config    # Store in main config
/set credential_storage_mode external  # Store in .credentials

/set credential_config_encrypt ON      # Enable encryption
/set credential_config_encrypt OFF     # Disable encryption

/set credential_external_file .credentials  # External filename (not path!)
```

### Files

```
~/.irssi/config                    # Main config
~/.irssi/.credentials              # External credentials (default name)
~/.irssi/<custom_name>             # External credentials (if renamed)
~/.irssi/config.backup.*           # YOUR manual backups (create these yourself!)
```

---

**Happy secure chatting!** 🔒
