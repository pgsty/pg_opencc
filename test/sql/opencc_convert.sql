CREATE EXTENSION opencc;

-- built-in configs
SELECT name, source, target, phrases, maturity FROM opencc.configs;

-- glyph conversion vs. regional vocabulary
SELECT opencc.s2t('鼠标里面的硅二极管坏了，导致光标分辨率降低。')   AS s2t,
       opencc.s2tw('鼠标里面的硅二极管坏了，导致光标分辨率降低。')  AS s2tw,
       opencc.s2twp('鼠标里面的硅二极管坏了，导致光标分辨率降低。') AS s2twp;
SELECT opencc.tw2sp('滑鼠裡面的矽二極體壞了，導致游標解析度降低。') AS tw2sp;

-- phrase-aware: the same characters convert differently in context
SELECT opencc.s2twp('海内存知己') AS poem, opencc.s2twp('安装内存条') AS hardware;
SELECT opencc.s2t('太后的头发很干燥。') AS s2t;
SELECT opencc.s2hkp('诺兰的电影') AS hk, opencc.s2twp('诺兰的电影') AS tw;
SELECT opencc.t2jp('舊字體歷史假名遣') AS t2jp, opencc.jp2t('旧字体歴史仮名遣') AS jp2t;

-- every wrapper is the same as convert(input, '<name>')
SELECT count(*) AS mismatches
  FROM opencc.configs c,
       LATERAL (SELECT '汉字與漢字 里面 裡面 后来 頭髮' AS x) s,
       LATERAL (SELECT opencc.convert(s.x, c.name) AS via_convert) a,
       LATERAL (SELECT CASE c.name
                  WHEN 's2t' THEN opencc.s2t(s.x)     WHEN 't2s' THEN opencc.t2s(s.x)
                  WHEN 's2tw' THEN opencc.s2tw(s.x)   WHEN 'tw2s' THEN opencc.tw2s(s.x)
                  WHEN 's2hk' THEN opencc.s2hk(s.x)   WHEN 'hk2s' THEN opencc.hk2s(s.x)
                  WHEN 's2twp' THEN opencc.s2twp(s.x) WHEN 'tw2sp' THEN opencc.tw2sp(s.x)
                  WHEN 's2hkp' THEN opencc.s2hkp(s.x) WHEN 'hk2sp' THEN opencc.hk2sp(s.x)
                  WHEN 't2tw' THEN opencc.t2tw(s.x)   WHEN 'tw2t' THEN opencc.tw2t(s.x)
                  WHEN 't2hk' THEN opencc.t2hk(s.x)   WHEN 'hk2t' THEN opencc.hk2t(s.x)
                  WHEN 't2jp' THEN opencc.t2jp(s.x)   WHEN 'jp2t' THEN opencc.jp2t(s.x)
                END AS via_wrapper) w
 WHERE a.via_convert IS DISTINCT FROM w.via_wrapper;

-- non-Chinese text, empty strings and NULL pass through
SELECT opencc.s2t('PostgreSQL 18 🐘 ½') AS ascii, opencc.s2t('') = '' AS empty, opencc.s2t(NULL) IS NULL AS null_in;
SELECT opencc.convert(NULL, 's2t') IS NULL AS null_input, opencc.convert('汉字', NULL) IS NULL AS null_config;

-- CJK compatibility ideographs are normalized first (U+F9F4 → 林)
SELECT opencc.s2t(U&'\F9F4') = '林' AS normalized;

-- long input
SELECT length(opencc.s2t(repeat('软件开发与数据库管理；', 10000))) AS len,
       opencc.s2t(repeat('软件开发与数据库管理；', 10000)) = repeat('軟件開發與數據庫管理；', 10000) AS same;

-- unknown config names are rejected with the list of valid ones
SELECT opencc.convert('汉字', 's2tww');
SELECT opencc.convert('汉字', 'S2T');

-- functions are usable in indexes and generated columns
CREATE TABLE doc (id int PRIMARY KEY, title text NOT NULL,
                  title_tw text GENERATED ALWAYS AS (opencc.s2twp(title)) STORED);
INSERT INTO doc (id, title) VALUES (1, '輝達'), (2, '英伟达'), (3, '辉达'), (4, '演算法'), (5, '算法');
CREATE INDEX doc_fold_idx ON doc (opencc.tw2sp(title));
SELECT id, title, title_tw FROM doc ORDER BY id;

SET enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT id FROM doc WHERE opencc.tw2sp(title) = opencc.tw2sp('英伟达');
EXPLAIN (COSTS OFF) SELECT id FROM doc WHERE opencc.convert(title, 'tw2sp') = '英伟达';
RESET enable_seqscan;

SELECT q, (SELECT string_agg(title, ' / ' ORDER BY id) FROM doc WHERE opencc.t2s(title) = opencc.t2s(q))     AS by_t2s,
          (SELECT string_agg(title, ' / ' ORDER BY id) FROM doc WHERE opencc.tw2sp(title) = opencc.tw2sp(q)) AS by_tw2sp
  FROM (VALUES ('英伟达'), ('算法')) v(q);

-- parallel workers can run the functions
SELECT p.proname, p.provolatile, p.proparallel
  FROM pg_proc p WHERE p.pronamespace = 'opencc'::regnamespace AND p.proname IN ('convert', 's2t', 'localize', 'detect')
 ORDER BY 1;

DROP TABLE doc;
