box.cfg{
   listen = 3301,
   admin  = 3302,
   log_level = 5,
   logger = 'tarantool.log',
   pid_file = 'box.pid',
   background = true,
}

if #box.space._user.index['name']:select{'test'} < 1 then
   box.schema.user.create('test', {password='test'})
end
