# Contributing to erssi

Thank you for your interest in contributing to erssi! This document provides guidelines for contributing to the project.

## Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/erssi.git
   cd erssi
   ```
3. **Set up the development environment**:
   ```bash
   ./install-dev.sh  # or manually install dependencies
   meson setup Build -Dwith-perl=yes -Dwith-otr=yes
   ninja -C Build
   ```

## Development Workflow

### 1. Create a Branch

```bash
# Create a feature branch
git checkout -b feat/your-feature-name

# Or a bugfix branch
git checkout -b fix/issue-description
```

### 2. Make Changes

- Follow the existing code style
- Add tests for new features
- Update documentation as needed
- Ensure code compiles without warnings:
  ```bash
  ninja -C Build
  ```

### 3. Test Your Changes

```bash
# Run tests
ninja -C Build test

# Test the build
./Build/src/fe-text/irssi --version
```

## Commit Message Guidelines

erssi follows the **Conventional Commits** specification for commit messages. This enables automated changelog generation and helps maintain a clear project history.

### Commit Message Format

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

### Examples

```
feat: add WebSocket compression support

Implement per-message deflate compression for fe-web module.
Reduces bandwidth usage by ~60% for typical IRC traffic.

Closes #123
```

```
fix(sidepanels): prevent emoji overflow in nicklist

Replace manual width calculation with grapheme cluster detection
to properly handle complex emoji like 👨‍👩‍👧‍👦.

Fixes #456
```

```
docs: update CLAUDE.md with release workflow

Add documentation for maintainers about the automated release
process using GitHub Actions.
```

### Commit Types

| Type | Description | When to Use |
|------|-------------|-------------|
| `feat` | New feature | Adding new functionality |
| `fix` | Bug fix | Fixing a bug or issue |
| `docs` | Documentation | Documentation-only changes |
| `style` | Code style | Formatting, whitespace, semicolons |
| `refactor` | Code refactoring | Neither fixes nor adds features |
| `perf` | Performance | Performance improvements |
| `test` | Tests | Adding or updating tests |
| `build` | Build system | Changes to build configuration |
| `ci` | CI/CD | Changes to GitHub Actions, etc. |
| `chore` | Maintenance | Other changes (dependencies, etc.) |
| `revert` | Revert | Reverting a previous commit |

### Scopes (Optional)

Scopes help identify which part of the codebase is affected:

- `sidepanels` - Sidepanel system
- `fe-web` - Web interface
- `credentials` - Credential management
- `mouse` - Mouse gesture system
- `core` - Core IRC functionality
- `perl` - Perl scripting integration
- `otr` - OTR encryption
- `build` - Build system

### Rules

1. **Use lowercase** for type and scope
2. **Start description with lowercase** (except proper nouns)
3. **No period** at the end of the description
4. **Use imperative mood**: "add feature" not "added feature"
5. **Keep first line under 100 characters**
6. **Reference issues** in footer: `Fixes #123`, `Closes #456`
7. **Breaking changes**: Add `BREAKING CHANGE:` in footer or `!` after type:
   ```
   feat!: remove deprecated credential API

   BREAKING CHANGE: The old credential_get_plaintext() function
   has been removed. Use credential_get() instead.
   ```

## Pull Request Process

### Before Submitting

1. **Rebase on main**:
   ```bash
   git fetch origin
   git rebase origin/main
   ```

2. **Ensure all tests pass**:
   ```bash
   ninja -C Build test
   ```

3. **Lint your commits** (automatic in PRs):
   ```bash
   # Install commitlint locally (optional)
   npm install -g @commitlint/cli @commitlint/config-conventional
   echo "feat: test message" | commitlint
   ```

### Submitting the PR

1. **Push your branch**:
   ```bash
   git push origin feat/your-feature-name
   ```

2. **Create Pull Request** on GitHub with:
   - **Title** following conventional commit format:
     ```
     feat: add WebSocket compression support
     ```
   - **Description** explaining:
     - What problem does this solve?
     - How does it solve it?
     - Any breaking changes?
     - Screenshots (if UI changes)
   - **Link related issues**: `Fixes #123`

3. **Wait for CI checks** to pass:
   - ✅ Build succeeds
   - ✅ Tests pass
   - ✅ Commit messages validated
   - ✅ PR title validated

4. **Address review feedback**:
   - Make requested changes
   - Push new commits (will be squashed on merge)
   - Respond to reviewer comments

### PR Title Validation

PR titles must follow conventional commit format because they become the squash commit message:

✅ **Good PR titles:**
- `feat: add SOCKS5 proxy support`
- `fix(sidepanels): prevent crash on empty nicklist`
- `docs: update installation instructions for macOS`

❌ **Bad PR titles:**
- `Add feature` (missing type)
- `FIX: Bug` (type should be lowercase)
- `fix bug.` (has trailing period)

## Code Style Guidelines

### C Code

- **Indentation**: Tabs (existing codebase style)
- **Line length**: Aim for <100 characters
- **Braces**: K&R style (opening brace on same line)
- **Naming**:
  - Functions: `lowercase_with_underscores()`
  - Structs: `UPPERCASE_REC`
  - Macros: `UPPERCASE_WITH_UNDERSCORES`
- **Comments**: Use `/* */` for block comments, `//` sparingly

### Example

```c
/* Good */
static void process_message(MESSAGE_REC *msg, const char *text)
{
	if (msg == NULL || text == NULL)
		return;

	char *processed = g_strdup(text);
	/* Process the message */
	signal_emit("message processed", 2, msg, processed);
	g_free(processed);
}
```

### Documentation

- Update relevant `.md` files in `docs/`
- Add/update code comments for complex logic
- Update `CLAUDE.md` for architectural changes
- Keep `CHANGELOG.md` up-to-date (auto-generated, but can be manually edited)

## Reporting Issues

### Bug Reports

Include:
- **erssi version**: `./Build/src/fe-text/irssi --version`
- **Operating system**: macOS/Linux distribution and version
- **Steps to reproduce**
- **Expected behavior**
- **Actual behavior**
- **Relevant logs** or error messages

### Feature Requests

Include:
- **Use case**: What problem does this solve?
- **Proposed solution**: How should it work?
- **Alternatives considered**: Other approaches?
- **Additional context**: Screenshots, examples, etc.

## Community Guidelines

- **Be respectful** and constructive
- **Search existing issues** before creating new ones
- **Stay on topic** in discussions
- **Help others** when you can
- **Follow the Code of Conduct** (coming soon)

## Development Resources

### Architecture Documentation

- `CLAUDE.md` - AI-friendly architecture overview
- `docs/` - User and developer documentation
- `README.md` - Project overview

### Key Directories

- `src/core/` - Core IRC protocol
- `src/irc/` - IRC-specific features
- `src/fe-text/` - Terminal UI (including sidepanels)
- `src/fe-web/` - Web interface
- `src/perl/` - Perl integration
- `tests/` - Test suites

### Testing

```bash
# Run all tests
ninja -C Build test

# Run specific test
./Build/tests/irc/core/test-irc
./Build/tests/fe-text/test-paste-join-multiline

# Run under valgrind (memory leak detection)
valgrind --leak-check=full ./Build/src/fe-text/irssi
```

### Useful Commands

```bash
# Rebuild after changes
ninja -C Build

# Clean rebuild
ninja -C Build clean
ninja -C Build

# Install locally for testing
ninja -C Build install

# Run erssi
./Build/src/fe-text/irssi
```

## Questions?

- **GitHub Issues**: For bugs and feature requests
- **GitHub Discussions**: For questions and general discussion
- **IRC**: `#erssi` on IRCnet (when available)

## License

By contributing to erssi, you agree that your contributions will be licensed under the GPL-2.0+ license.

---

Thank you for contributing to erssi! 🎉
