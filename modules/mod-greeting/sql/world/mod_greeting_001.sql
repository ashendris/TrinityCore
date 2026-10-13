-- mod-greeting PoC: uniquely named, idempotent informational string.
INSERT INTO `trinity_string` (`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES (31000, 'mod-greeting is loaded.', NULL, NULL, NULL, NULL, NULL, NULL, NULL)
ON DUPLICATE KEY UPDATE `content_default` = VALUES(`content_default`);
