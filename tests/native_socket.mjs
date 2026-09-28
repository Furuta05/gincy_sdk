import { PGlite } from '@electric-sql/pglite'
import { PGLiteSocketServer } from '@electric-sql/pglite-socket'
import { spawn } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const binary = path.resolve(process.argv[2] || 'build/pool_test')
const migration = fileURLToPath(new URL('../garrysmod/lua/gincy/legacy_rpg/migrations/001_framework.sql', import.meta.url))
const db = await PGlite.create()
const server = new PGLiteSocketServer({ db, host: '127.0.0.1', port: 55432 })
await server.start()
let code = 1
try {
    const child = spawn(binary, [], {
        stdio: 'inherit',
        env: { ...process.env, GINCY_TEST_PORT: '55432', GINCY_PGLITE_TEST: '1', GINCY_TEST_MIGRATION: migration }
    })
    code = await new Promise((resolve, reject) => { child.on('exit', resolve); child.on('error', reject) })
} finally {
    await server.stop()
    await db.close()
}
process.exit(code ?? 1)
