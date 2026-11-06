/* opencc--0.1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION opencc" to load this file. \quit

GRANT USAGE ON SCHEMA @extschema@ TO PUBLIC;

-- ---------------------------------------------------------------------------
-- Conversion with a built-in OpenCC config.
-- The dictionaries are compiled into the library, so a given extension
-- version always returns the same output: these functions are IMMUTABLE and
-- may be used in expression indexes, generated columns and constraints.
-- ---------------------------------------------------------------------------
CREATE FUNCTION @extschema@.convert(input text, config text)
RETURNS text
AS 'MODULE_PATHNAME', 'opencc_convert'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE COST 50;
COMMENT ON FUNCTION @extschema@.convert(text, text) IS
  'Convert text with a built-in OpenCC config, e.g. convert(body, ''s2twp'')';

CREATE FUNCTION @extschema@.s2t(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 's2t');
CREATE FUNCTION @extschema@.t2s(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 't2s');
CREATE FUNCTION @extschema@.s2tw(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 's2tw');
CREATE FUNCTION @extschema@.tw2s(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'tw2s');
CREATE FUNCTION @extschema@.s2hk(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 's2hk');
CREATE FUNCTION @extschema@.hk2s(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'hk2s');
CREATE FUNCTION @extschema@.s2twp(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 's2twp');
CREATE FUNCTION @extschema@.tw2sp(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'tw2sp');
CREATE FUNCTION @extschema@.s2hkp(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 's2hkp');
CREATE FUNCTION @extschema@.hk2sp(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'hk2sp');
CREATE FUNCTION @extschema@.t2tw(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 't2tw');
CREATE FUNCTION @extschema@.tw2t(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'tw2t');
CREATE FUNCTION @extschema@.t2hk(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 't2hk');
CREATE FUNCTION @extschema@.hk2t(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'hk2t');
CREATE FUNCTION @extschema@.t2jp(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 't2jp');
CREATE FUNCTION @extschema@.jp2t(input text) RETURNS text
LANGUAGE sql IMMUTABLE STRICT PARALLEL SAFE RETURN @extschema@.convert(input, 'jp2t');

COMMENT ON FUNCTION @extschema@.s2t(text)   IS 'Simplified Chinese to Traditional Chinese (OpenCC standard)';
COMMENT ON FUNCTION @extschema@.t2s(text)   IS 'Traditional Chinese (OpenCC standard) to Simplified Chinese';
COMMENT ON FUNCTION @extschema@.s2tw(text)  IS 'Simplified Chinese to Traditional Chinese (Taiwan standard)';
COMMENT ON FUNCTION @extschema@.tw2s(text)  IS 'Traditional Chinese (Taiwan standard) to Simplified Chinese';
COMMENT ON FUNCTION @extschema@.s2hk(text)  IS 'Simplified Chinese to Traditional Chinese (Hong Kong variant)';
COMMENT ON FUNCTION @extschema@.hk2s(text)  IS 'Traditional Chinese (Hong Kong variant) to Simplified Chinese';
COMMENT ON FUNCTION @extschema@.s2twp(text) IS 'Simplified Chinese to Traditional Chinese (Taiwan standard) with Taiwan phrases';
COMMENT ON FUNCTION @extschema@.tw2sp(text) IS 'Traditional Chinese (Taiwan standard) to Simplified Chinese with Mainland phrases';
COMMENT ON FUNCTION @extschema@.s2hkp(text) IS 'Simplified Chinese to Traditional Chinese (Hong Kong variant) with Hong Kong phrases';
COMMENT ON FUNCTION @extschema@.hk2sp(text) IS 'Traditional Chinese (Hong Kong variant) to Simplified Chinese with Mainland phrases';
COMMENT ON FUNCTION @extschema@.t2tw(text)  IS 'Traditional Chinese (OpenCC standard) to Taiwan standard';
COMMENT ON FUNCTION @extschema@.tw2t(text)  IS 'Traditional Chinese (Taiwan standard) to OpenCC standard';
COMMENT ON FUNCTION @extschema@.t2hk(text)  IS 'Traditional Chinese (OpenCC standard) to Hong Kong variant';
COMMENT ON FUNCTION @extschema@.hk2t(text)  IS 'Traditional Chinese (Hong Kong variant) to OpenCC standard';
COMMENT ON FUNCTION @extschema@.t2jp(text)  IS 'Japanese Kyujitai (old Kanji) to Shinjitai (new Kanji)';
COMMENT ON FUNCTION @extschema@.jp2t(text)  IS 'Japanese Shinjitai (new Kanji) to Kyujitai (old Kanji)';

CREATE FUNCTION @extschema@._configs(
    OUT name text, OUT source text, OUT target text, OUT phrases boolean,
    OUT maturity text, OUT description text, OUT loaded boolean)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'opencc_configs'
LANGUAGE C STABLE PARALLEL SAFE ROWS 16;

CREATE VIEW @extschema@.configs AS SELECT * FROM @extschema@._configs();
COMMENT ON VIEW @extschema@.configs IS
  'Built-in OpenCC configs; loaded tells whether the current backend has it in memory';

-- ---------------------------------------------------------------------------
-- Region-oriented conversion and script detection.
-- ---------------------------------------------------------------------------
CREATE FUNCTION @extschema@.localize(input text, target text, source text DEFAULT 'auto')
RETURNS text
AS 'MODULE_PATHNAME', 'opencc_localize'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE COST 100;
COMMENT ON FUNCTION @extschema@.localize(text, text, text) IS
  'Convert text for a region: zh-Hans, zh-Hant, zh-CN, zh-TW, zh-HK or ja. '
  'source => ''auto'' only detects the script; name the source region to convert regional phrases';

CREATE FUNCTION @extschema@._routes(
    OUT source text, OUT target text, OUT chain text[], OUT maturity text)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'opencc_routes'
LANGUAGE C IMMUTABLE PARALLEL SAFE ROWS 36;

CREATE VIEW @extschema@.routes AS SELECT * FROM @extschema@._routes();
COMMENT ON VIEW @extschema@.routes IS 'Config chain used by localize() for each source and target region';

CREATE FUNCTION @extschema@.detect(input text)
RETURNS text
AS 'MODULE_PATHNAME', 'opencc_detect'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE COST 10;
COMMENT ON FUNCTION @extschema@.detect(text) IS
  'Heuristic script of the text: none, neutral, simplified, traditional, mixed or japanese';

CREATE FUNCTION @extschema@.script_stats(input text,
    OUT han integer, OUT simplified integer, OUT traditional integer, OUT kana integer)
AS 'MODULE_PATHNAME', 'opencc_script_stats'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE COST 10;
COMMENT ON FUNCTION @extschema@.script_stats(text) IS
  'Counts of CJK ideographs, simplified-only and traditional-only characters, and kana';

-- ---------------------------------------------------------------------------
-- Inspection: why a conversion came out the way it did.
-- ---------------------------------------------------------------------------
CREATE FUNCTION @extschema@.explain(input text, config text)
RETURNS TABLE (seq integer, source text, steps text[], output text)
AS 'MODULE_PATHNAME', 'opencc_explain'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE ROWS 20;
COMMENT ON FUNCTION @extschema@.explain(text, text) IS
  'Segments of the input and the result after each conversion stage';

CREATE FUNCTION @extschema@.ambiguities(input text, config text)
RETURNS TABLE (pos integer, len integer, source text, output text, candidates text[])
AS 'MODULE_PATHNAME', 'opencc_ambiguities'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE ROWS 5;
COMMENT ON FUNCTION @extschema@.ambiguities(text, text) IS
  'Output spans produced by one-to-many dictionary entries, with every candidate';

CREATE FUNCTION @extschema@.candidates(word text, config text)
RETURNS text[]
AS 'MODULE_PATHNAME', 'opencc_candidates'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
COMMENT ON FUNCTION @extschema@.candidates(text, text) IS
  'All forms a word can take under a config, default first; empty when no dictionary has it';

-- ---------------------------------------------------------------------------
-- Operations.
-- ---------------------------------------------------------------------------
CREATE FUNCTION @extschema@.version(OUT extension text, OUT opencc text, OUT dictionary text)
AS 'MODULE_PATHNAME', 'opencc_version'
LANGUAGE C STABLE PARALLEL SAFE;
COMMENT ON FUNCTION @extschema@.version() IS
  'Extension version, vendored OpenCC release and digest of the embedded dictionaries';

CREATE FUNCTION @extschema@.dependents()
RETURNS TABLE (kind text, object text, uses text, action text)
LANGUAGE sql STABLE
BEGIN ATOMIC
  WITH dep AS (
    SELECT d.classid, d.objid, d.objsubid, d.refobjid,
           CASE d.classid
             WHEN 'pg_class'::regclass      THEN d.objid
             WHEN 'pg_attrdef'::regclass    THEN (SELECT ad.adrelid FROM pg_attrdef ad WHERE ad.oid = d.objid)
             WHEN 'pg_constraint'::regclass THEN (SELECT co.conrelid FROM pg_constraint co WHERE co.oid = d.objid)
             WHEN 'pg_rewrite'::regclass    THEN (SELECT rw.ev_class FROM pg_rewrite rw WHERE rw.oid = d.objid)
           END AS rel
      FROM pg_depend d
      JOIN pg_proc p ON p.oid = d.refobjid
     WHERE d.refclassid = 'pg_proc'::regclass
       AND d.deptype = 'n'
       AND p.pronamespace = (SELECT e.extnamespace FROM pg_extension e WHERE e.extname = 'opencc')
  ), obj AS (
    SELECT dep.*, c.relkind, c.relnamespace,
           CASE
             WHEN dep.classid = 'pg_attrdef'::regclass THEN
               (SELECT a.attgenerated FROM pg_attrdef ad
                  JOIN pg_attribute a ON a.attrelid = ad.adrelid AND a.attnum = ad.adnum
                 WHERE ad.oid = dep.objid)::text
           END AS generated,
           CASE
             WHEN dep.classid = 'pg_attrdef'::regclass THEN
               (SELECT a.attname FROM pg_attrdef ad
                  JOIN pg_attribute a ON a.attrelid = ad.adrelid AND a.attnum = ad.adnum
                 WHERE ad.oid = dep.objid)::text
           END AS colname,
           CASE
             WHEN dep.classid = 'pg_attrdef'::regclass THEN
               (SELECT ad.adnum FROM pg_attrdef ad WHERE ad.oid = dep.objid)::integer
             ELSE 0
           END AS colnum
      FROM dep JOIN pg_class c ON c.oid = dep.rel
  )
  SELECT DISTINCT
         CASE
           WHEN obj.classid = 'pg_attrdef'::regclass    THEN 'generated column'
           WHEN obj.classid = 'pg_constraint'::regclass THEN 'constraint'
           WHEN obj.relkind = 'm'                       THEN 'materialized view'
           ELSE 'index'
         END,
         CASE
           WHEN obj.classid IN ('pg_attrdef'::regclass, 'pg_rewrite'::regclass) THEN
             pg_describe_object('pg_class'::regclass, obj.rel, obj.colnum)
           ELSE pg_describe_object(obj.classid, obj.objid, obj.objsubid)
         END,
         obj.refobjid::regprocedure::text,
         CASE
           WHEN obj.classid = 'pg_attrdef'::regclass THEN
             format('UPDATE %s SET %I = DEFAULT', obj.rel::regclass, obj.colname)
           WHEN obj.classid = 'pg_constraint'::regclass THEN
             (SELECT format('ALTER TABLE %s DROP CONSTRAINT %I, ADD CONSTRAINT %I %s NOT VALID; '
                            'ALTER TABLE %s VALIDATE CONSTRAINT %I',
                            obj.rel::regclass, co.conname, co.conname, pg_get_constraintdef(co.oid),
                            obj.rel::regclass, co.conname)
                FROM pg_constraint co WHERE co.oid = obj.objid)
           WHEN obj.relkind = 'm' THEN format('REFRESH MATERIALIZED VIEW %s', obj.rel::regclass)
           ELSE format('REINDEX INDEX CONCURRENTLY %s', obj.rel::regclass)
         END
    FROM obj
   WHERE obj.relnamespace <> (SELECT e.extnamespace FROM pg_extension e WHERE e.extname = 'opencc')
     AND (obj.classid = 'pg_constraint'::regclass
          OR (obj.classid = 'pg_attrdef'::regclass AND obj.generated = 's')
          OR (obj.classid = 'pg_class'::regclass AND obj.relkind IN ('i', 'I'))
          OR (obj.classid = 'pg_rewrite'::regclass AND obj.relkind = 'm'))
   ORDER BY 1, 2, 3;
END;
COMMENT ON FUNCTION @extschema@.dependents() IS
  'Stored values derived from opencc functions (indexes, stored generated columns, CHECK '
  'constraints, materialized views) and the statement that rebuilds each after an upgrade';

GRANT SELECT ON @extschema@.configs, @extschema@.routes TO PUBLIC;
