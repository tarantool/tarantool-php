os = require('os')
require('console').listen(os.getenv('ADMIN'))
box.cfg{
   listen           = os.getenv('LISTEN'),
   log_level        = 5,
   logger           = 'tarantool.log',
   pid_file         = 'box.pid',
   background       = true,
   slab_alloc_arena = 4
}

lp = {
   test = 'test',
   test_empty = '',
   test_big = '123456789012345678901234567890123456789012345678901234567890' -- '1234567890' * 6
}

for k, v in pairs(lp) do
   print(k, v)
   if #box.space._user.index.name:select{k} == 0 then
      box.schema.user.create(k, { password = v })
      if k == 'test' then
         box.schema.user.grant('test', 'read', 'space', '_space')
         box.schema.user.grant('test', 'read', 'space', '_index')
         box.schema.user.grant('test', 'execute', 'universe')
      end
   end
end

if not box.space.test then
   box.schema.space.create('test')
   box.space.test:create_index('primary',   {type = 'TREE', unique = true, parts = {1, 'NUM'}})
   box.space.test:create_index('secondary', {type = 'TREE', unique = false, parts = {2, 'NUM', 3, 'STR'}})
   box.schema.user.grant('test', 'read,write', 'space', 'test')
end
