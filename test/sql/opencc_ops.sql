-- versions: the dictionary digest identifies the embedded data
SELECT extension, opencc, dictionary ~ '^sha256:[0-9a-f]{64}$' AS digest_ok FROM opencc.version();

-- configs are loaded on first use, once per backend
SELECT count(*) AS loaded FROM opencc.configs WHERE loaded;
SELECT opencc.t2s('漢字');
SELECT name FROM opencc.configs WHERE loaded;

-- objects that store results derived from opencc, and how to rebuild them
CREATE SCHEMA app;
CREATE TABLE app.post (id int PRIMARY KEY, body text NOT NULL,
  body_tw text GENERATED ALWAYS AS (opencc.localize(body, 'zh-TW')) STORED,
  CONSTRAINT body_simplified CHECK (opencc.detect(body) <> 'traditional'));
CREATE INDEX post_fold_idx ON app.post (opencc.t2s(body));
CREATE MATERIALIZED VIEW app.post_hk AS SELECT id, opencc.s2hk(body) AS body FROM app.post;
CREATE VIEW app.post_v AS SELECT id, opencc.s2t(body) AS body FROM app.post;

SELECT kind, object, uses FROM opencc.dependents();
SELECT action FROM opencc.dependents();

-- the suggested statements run
UPDATE app.post SET body_tw = DEFAULT;
REINDEX INDEX CONCURRENTLY app.post_fold_idx;
REFRESH MATERIALIZED VIEW app.post_hk;
ALTER TABLE app.post DROP CONSTRAINT body_simplified,
  ADD CONSTRAINT body_simplified CHECK ((opencc.detect(body) <> 'traditional'::text)) NOT VALID;
ALTER TABLE app.post VALIDATE CONSTRAINT body_simplified;

DROP SCHEMA app CASCADE;
SELECT count(*) AS left_over FROM opencc.dependents();

-- non-superusers can use the extension
CREATE ROLE regress_opencc_reader;
SET ROLE regress_opencc_reader;
SELECT opencc.s2t('汉字'), (SELECT count(*) FROM opencc.configs) AS configs, (SELECT count(*) FROM opencc.routes) AS routes;
RESET ROLE;
DROP ROLE regress_opencc_reader;
