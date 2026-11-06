-- region routes
SELECT * FROM opencc.routes ORDER BY source, target;

-- one source, every region
SELECT r AS target, opencc.localize('克里斯托弗·诺兰的电影，安装内存条', r, 'zh-CN') AS output
  FROM unnest(ARRAY['zh-Hans', 'zh-Hant', 'zh-CN', 'zh-TW', 'zh-HK']) r;

-- region tags are case-insensitive and accept "_" for "-"
SELECT opencc.localize('诺兰的电影', 'zh-tw') = opencc.localize('诺兰的电影', 'ZH_TW') AS same;

-- naming the source region converts vocabulary; auto only detects the script
SELECT opencc.localize('滑鼠裡面的矽二極體壞了', 'zh-CN', source => 'zh-TW') AS from_tw,
       opencc.localize('滑鼠裡面的矽二極體壞了', 'zh-CN')                    AS from_auto;
SELECT opencc.localize('鼠标', 'zh-TW') AS simplified_in, opencc.localize('滑鼠', 'zh-TW') AS tw_in;
SELECT opencc.localize('旧字体の歴史', 'zh-Hant') AS japanese_in;

-- errors
SELECT opencc.localize('诺兰', 'zh-SG');
SELECT opencc.localize('诺兰', 'zh-TW', 'en');

-- script detection
SELECT x AS input, opencc.detect(x), s.*
  FROM (VALUES ('为什么你在床里面睡着？'), ('為什麼你在床裡面睡著？'), ('抬头 擡头 抬頭 擡頭'),
               ('旧字体の歴史'), ('PostgreSQL 18'), ('中文'), ('')) v(x),
       LATERAL opencc.script_stats(x) s;

-- detect() in a CHECK constraint
CREATE TABLE article (id int PRIMARY KEY, body text NOT NULL
  CONSTRAINT body_is_simplified CHECK (opencc.detect(body) IN ('simplified', 'neutral', 'none')));
INSERT INTO article VALUES (1, '为什么你在床里面睡着？');
INSERT INTO article VALUES (2, '為什麼你在床裡面睡著？');
DROP TABLE article;
