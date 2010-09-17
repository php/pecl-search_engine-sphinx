<?php
$s = new SphinxClient;
$s->setServer('localhost',9312);
$s->setMatchMode(SPH_MATCH_ANY);
$s->setMaxQueryTime(3);


$result = $s->query('test');
var_dump($result);

if ($result === false)
{
    print "Query failed: " . $s->GetLastError() . ".\n";
} 
