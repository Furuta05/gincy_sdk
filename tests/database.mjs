import { PGlite } from '@electric-sql/pglite'
import { readFile } from 'node:fs/promises'
import assert from 'node:assert/strict'
const db = await PGlite.create()
const sql = await readFile(new URL('../garrysmod/lua/gincy/legacy_rpg/migrations/001_framework.sql', import.meta.url), 'utf8')
let assertions = 0
const check = (value) => { assert(value); assertions++ }
const query = async (sql, params = []) => (await db.query(sql, params)).rows
const rejected = async (sql, params, text) => { let error; try { await db.query(sql, params) } catch (e) { error = e } check(error && error.message.includes(text)) }
await db.exec(sql)
await db.exec(sql)
const a = (await query("SELECT * FROM gincy_create_character('account_a','Alice','models/player/Group01/male_07.mdl')"))[0]
const b = (await query("SELECT * FROM gincy_create_character('account_b','Boris','models/player/Group01/male_07.mdl')"))[0]
const ca = (await query('SELECT id FROM gincy_containers WHERE owner_character=$1', [a.id]))[0].id
const cb = (await query('SELECT id FROM gincy_containers WHERE owner_character=$1', [b.id]))[0].id
const add = 'SELECT gincy_inventory_change($1,$2,$3,$4,$5,$6::jsonb,$7)'
await query(add, [ca,'apple',25,20,.2,'{}',1])
check((await query('SELECT count(*)::int AS n FROM gincy_inventory WHERE container_id=$1',[ca]))[0].n === 2)
await query('SELECT gincy_inventory_transfer($1,$2,$3,$4)',[ca,cb,'apple',7])
check((await query('SELECT sum(amount)::int AS n FROM gincy_inventory WHERE container_id=$1',[ca]))[0].n === 18)
await rejected('SELECT gincy_inventory_transfer($1,$2,$3,$4)',[ca,cb,'apple',100],'INSUFFICIENT_ITEMS')
check((await query('SELECT sum(amount)::int AS n FROM gincy_inventory WHERE container_id=$1',[cb]))[0].n === 7)
await rejected(add,[ca,'iron_ore',100,10,2,'{}',1],'CAPACITY_EXCEEDED')
check((await query("SELECT count(*)::int AS n FROM gincy_inventory WHERE item='iron_ore'"))[0].n === 0)
await query(add,[ca,'coat',1,1,2,'{"color":"blue"}',.7])
const unique = (await query("SELECT instance FROM gincy_inventory WHERE item='coat'"))[0].instance
await query('SELECT gincy_inventory_transfer($1,$2,$3,$4,$5)',[ca,cb,'coat',1,unique])
check((await query('SELECT instance,container_id,durability FROM gincy_inventory WHERE instance=$1',[unique]))[0].container_id === cb)
await query('INSERT INTO gincy_equipment(character_id,slot,instance) VALUES($1,$2,$3)',[b.id,'body',unique])
await rejected('SELECT gincy_inventory_transfer($1,$2,$3,$4,$5)',[cb,ca,'coat',1,unique],'INSUFFICIENT_ITEMS')
await query("INSERT INTO gincy_character_values VALUES($1,'data','magic.mana','100')",[a.id])
const snap = (await query('SELECT gincy_character_snapshot($1) AS s',[a.id]))[0].s
check(snap.name === 'Alice' && typeof snap.id === 'string' && snap.values[0].value === 100)
await query("SELECT * FROM gincy_create_character('account_a','Second','model')")
await query("SELECT * FROM gincy_create_character('account_a','Third','model')")
await rejected("SELECT * FROM gincy_create_character('account_a','Fourth','model')",[],'CHARACTER_LIMIT')
await query(add,[ca,'wood',5,20,1,'{}',1])
const activity = (await query("INSERT INTO gincy_activities(kind,owner_id) VALUES('resource_delivery',$1) RETURNING id",[a.id]))[0].id
await db.transaction(async tx => {
 await tx.query('SELECT id FROM gincy_activities WHERE id=$1 FOR UPDATE',[activity])
 await tx.query(add,[ca,'wood',-5,20,1,'{}',1])
 await tx.query('UPDATE gincy_characters SET money=money+25 WHERE id=$1',[a.id])
 await tx.query("UPDATE gincy_activities SET state='completed' WHERE id=$1",[activity])
})
check((await query('SELECT money::int FROM gincy_characters WHERE id=$1',[a.id]))[0].money === 25)
check((await query("SELECT count(*)::int AS n FROM gincy_inventory WHERE item='wood' AND container_id=$1",[ca]))[0].n === 0)
let rolledBack = false
try { await db.transaction(async tx => { await tx.query('UPDATE gincy_characters SET money=money+100 WHERE id=$1',[a.id]); await tx.query(add,[ca,'wood',-5,20,1,'{}',1]) }) } catch { rolledBack = true }
check(rolledBack)
check((await query('SELECT money::int FROM gincy_characters WHERE id=$1',[a.id]))[0].money === 25)
await query("INSERT INTO gincy_migrations(module,version,checksum) VALUES('test',1,'abc')")
await rejected("DO $m$ BEGIN IF EXISTS(SELECT 1 FROM gincy_migrations WHERE module='test' AND version=1 AND checksum<>'def') THEN RAISE EXCEPTION 'MIGRATION_CHANGED'; END IF; END $m$",[],'MIGRATION_CHANGED')
await db.close()
console.log(`PostgreSQL-compatible SQL assertions: ${assertions}`)
