INSERT INTO gincy_storage(module,key,value) VALUES('survival_example','schema','{"version":1}'::jsonb) ON CONFLICT(module,key) DO NOTHING;
