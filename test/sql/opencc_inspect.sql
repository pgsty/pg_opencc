-- one-to-many spans and their candidates
SELECT * FROM opencc.ambiguities('大战文丑的时候，他的头发很干燥', 's2t');
-- phrases settle one-to-many characters, so nothing is reported here
SELECT * FROM opencc.ambiguities('台风过后，我在里屋发现一张旧钟表', 's2t');
SELECT count(*) AS none FROM opencc.ambiguities('PostgreSQL', 's2t');

-- candidates: default first, empty when no dictionary has the word
SELECT opencc.candidates('里', 's2t')        AS s2t,
       opencc.candidates('里', 's2tw')       AS s2tw,
       opencc.candidates('文丑', 's2t')      AS phrase,
       opencc.candidates('PostgreSQL', 's2t') AS missing;

-- explain: segments and the result after each conversion stage
SELECT * FROM opencc.explain('他只看了几行日志，就一叶知秋，猜到整个系统是数据库连接池出了问题', 's2twp');
SELECT * FROM opencc.explain('汉字', 's2t');
SELECT count(*) AS rows FROM opencc.explain('', 's2twp');

-- explain output concatenates to the conversion result
SELECT string_agg(output, '' ORDER BY seq) = opencc.s2twp(x) AS consistent
  FROM (VALUES ('我们在老挝的服务器的硬盘需要使用互联网算法软件解决异步的问题。')) v(x),
       LATERAL opencc.explain(x, 's2twp')
 GROUP BY x;

SELECT * FROM opencc.ambiguities('汉字', 'nope');
