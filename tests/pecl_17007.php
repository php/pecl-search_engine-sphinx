<?php
$sphinx = new SphinxClient();
$sphinx->setServer('localhost', 3312);
$sphinx->setMatchMode(SPH_MATCH_EXTENDED2);
$sphinx->addQuery('test)');
$results = $sphinx->runQueries();
var_dump($results);
