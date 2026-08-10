import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const projectRoot = fileURLToPath(new URL('../../../../', import.meta.url));
const installer = join(projectRoot, 'scripts/install-skill.sh');
const sourceSkill = join(projectRoot, 'skills/ohospatch/SKILL.md');
const sourceDeclaration = join(projectRoot, 'skills/ohospatch/references/fixit.d.js');

function runInstaller(home, args = []) {
  return spawnSync(installer, args, {
    cwd: projectRoot,
    encoding: 'utf8',
    env: {
      ...process.env,
      HOME: home,
      CODEX_HOME: join(home, 'codex'),
      CLAUDE_HOME: join(home, 'claude')
    }
  });
}

test('OhosPatch Skill uses the short cross-tool name', async () => {
  const [skill, installerStat] = await Promise.all([
    readFile(sourceSkill, 'utf8'),
    stat(installer)
  ]);

  assert.match(skill, /^---\nname: ohospatch\n/m);
  assert.doesNotMatch(skill, /ohospatch-patch-authoring/);
  assert.notEqual(installerStat.mode & 0o111, 0, 'installer must be executable');
});

test('Skill installer supports Codex and Claude Code', async (context) => {
  const home = await mkdtemp(join(tmpdir(), 'ohospatch-skill-'));
  context.after(() => rm(home, { recursive: true, force: true }));

  const installed = runInstaller(home);
  assert.equal(installed.status, 0, installed.stderr);
  assert.match(installed.stdout, /Codex/);
  assert.match(installed.stdout, /Claude Code/);

  const [source, declaration, codexSkill, claudeSkill, codexDeclaration, claudeDeclaration] =
    await Promise.all([
      readFile(sourceSkill, 'utf8'),
      readFile(sourceDeclaration, 'utf8'),
      readFile(join(home, 'codex/skills/ohospatch/SKILL.md'), 'utf8'),
      readFile(join(home, 'claude/skills/ohospatch/SKILL.md'), 'utf8'),
      readFile(join(home, 'codex/skills/ohospatch/references/fixit.d.js'), 'utf8'),
      readFile(join(home, 'claude/skills/ohospatch/references/fixit.d.js'), 'utf8')
    ]);

  assert.equal(codexSkill, source);
  assert.equal(claudeSkill, source);
  assert.equal(codexDeclaration, declaration);
  assert.equal(claudeDeclaration, declaration);

  const duplicate = runInstaller(home);
  assert.equal(duplicate.status, 1);
  assert.match(duplicate.stderr, /--force/);

  const replaced = runInstaller(home, ['--all', '--force']);
  assert.equal(replaced.status, 0, replaced.stderr);
});

test('Skill installer can target Codex only', async (context) => {
  const home = await mkdtemp(join(tmpdir(), 'ohospatch-skill-codex-'));
  context.after(() => rm(home, { recursive: true, force: true }));

  const installed = runInstaller(home, ['--codex']);
  assert.equal(installed.status, 0, installed.stderr);
  assert.match(installed.stdout, /Codex/);
  assert.doesNotMatch(installed.stdout, /Claude Code/);

  const codexSkill = await readFile(join(home, 'codex/skills/ohospatch/SKILL.md'), 'utf8');
  assert.match(codexSkill, /^---\nname: ohospatch\n/m);

  await assert.rejects(
    readFile(join(home, 'claude/skills/ohospatch/SKILL.md'), 'utf8'),
    { code: 'ENOENT' }
  );
});
