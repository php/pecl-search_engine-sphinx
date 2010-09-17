<?php

require ( "api/sphinxapi-1.10.php" );

function test_query ( $client, $test_extras ) 
{
	$override_docid = 4;
	$override_value = 456;
	$query = "test";
	$index = "test1";

	$client->SetFieldWeights ( array ( 'title' => 100, 'content' => 1 ) );

	if ( $test_extras )
	{
		$client->SetOverride ( "group_id", SPH_ATTR_INTEGER, array ( $override_docid => $override_value) );
		$client->SetSelect ( "*, group_id*1000+@id*10 AS q" );
	}

	$client->SetArrayResult(true);
	$res = $client->Query ( $query, $index );
	if ( $res === false )
	{
		print "Query failed: " . $client->GetLastError() . ".\n";
	} 
	else 
	{
		if ( $client->GetLastWarning() )
			print "WARNING: " . $client->GetLastWarning() . "\n\n";

		print "Query '$query' retrieved $res[total] of $res[total_found] matches in $res[time] sec.\n";
		print "Query stats:\n";
		if ( is_array($res["words"]) )
			foreach ( $res["words"] as $word => $info )
				print "    '$word' found $info[hits] times in $info[docs] documents\n";
		print "\n";

		if ( is_array($res["matches"]) )
		{
			$n = 1;
			print "Matches:\n";
			foreach ( $res["matches"] as $docinfo )
			{
				print "$n. doc_id=$docinfo[id], weight=$docinfo[weight]";
				foreach ( $res["attrs"] as $attrname => $attrtype )
				{
					$value = $docinfo["attrs"][$attrname];
					if ( $attrtype & SPH_ATTR_MULTI )
					{
						$value = "(" . join ( ",", $value ) .")";
					} 
					else 
					{
						if ( $attrtype==SPH_ATTR_TIMESTAMP )
							$value = date ( "Y-m-d H:i:s", $value );
					}
					print ", $attrname=$value";
				}
				print "\n";
				$n++;
			}
		}
	}
	print "\n";
}

function test_excerpt ( $client )
{
	$docs = array
	(
		"this is my test text to be highlighted, and for the sake of the testing we need to pump its length somewhat",
		"another test text to be highlighted, below limit",
		"test number three, without phrase match",
		"final test, not only without phrase match, but also above limit and with swapped phrase text test as well"
	);
	$words = "test text";
	$index = "test1";
	$opts = array
	(
		"chunk_separator"	=> " ... ",
		"limit"				=> 60,
		"around"			=> 3,
	);

	for ( $j=0; $j<2; $j++ )
	{
		$opts["exact_phrase"] = $j;
		print "exact_phrase=$j\n" ;

		$res = $client->BuildExcerpts ( $docs, $index, $words, $opts );
		if ( !$res )
		{
			print "query failed: " . $client->GetLastError() . ".\n" ;
		} 
		else 
		{
			$n = 0;
			foreach ( $res as $entry )
			{
				$n++;
				print "n=$n, res=$entry\n";
			}
		}
		print "\n";
	}
}

function test_update ( $client )
{
	$attrs = array( "group_id" , "group_id2" );
	$vals = array( 2 => array ( 22, 33 ) , 4 => array ( 444, 555 ) );

	$res = $client->UpdateAttributes( "test1", $attrs, $vals);
	if ( $res < 0 )
		print "query failed: " . $client->GetLastError() . ".\n\n";
	else 
		print "update success, $res rows updated\n\n";
}

function test_update_mva ( $client )
{
	$attrs = array ( "mva" , "mva2" );
	$vals = array ( 
		1 => array( array ( 123, 456 ) , array ( 321 ) ) , 
		2 => array( array ( 78, 910, 11 ) , array ( 87, 12 ) ), 
		4 => array( array ( 55 ) , array ( 66 , 77 ) ) 
	);

	$res = $client->UpdateAttributes( "test1", $attrs, $vals, true );
	if ( $res === false || $res < 0 )
		print "query mva failed: " . $client->GetLastError() . ".\n\n";
	else 
		print "update mva success, $res rows updated\n\n";
}

function test_keywords ( $client )
{
	$words = $client->BuildKeywords("hello test one", "test1", true);
	
	if ( !$words )
	{
		print "build_keywords failed: " . $client->GetLastError() . "\n\n" ;
	} 
	else 
	{
		print "build_keywords result:\n";

		$n = 0;
		foreach ( $words as $entry )
		{
			$n++;
			printf ( "%d. tokenized=%s, normalized=%s, docs=%d, hits=%d\n", $n,
				$entry['tokenized'], $entry['normalized'],
				$entry['docs'], $entry['hits'] );
		}
		print "\n";
	}
}

function test_status ( $client )
{
	$status = $client->Status();
	if ( !$status )
	{
		print "status failed: " . $client->GetLastError() . "\n\n";
	}
	
	foreach ( $status as $item )
	{
		print "$item[0]: $item[1]\n";
	}
	print "\n";
}

date_default_timezone_set('Europe/Moscow');

$host = "localhost";
$port = 9312;

$client = new SphinxClient ();
$client->SetServer ( $host, $port );

test_query ( $client, false );
test_excerpt ( $client );
test_update ( $client );
test_update_mva ( $client );
test_query ( $client, false );

test_keywords ( $client );
test_query ( $client, true );

$client->Open();
test_update ( $client );
test_update ( $client );
test_query ( $client, false );
test_query ( $client, false );
$client->Close();

test_status ( $client );



