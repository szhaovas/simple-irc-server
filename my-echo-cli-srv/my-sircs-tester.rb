#! /usr/bin/env ruby
#
# CMPU-375 Simple IRC server tester
# Team: Junrui, Tahsin & Shihan 
# Tests-script for my-echo-cli-srv
# This script sends a sequence of commands to our IRC server and inspects
# the replies to see if it is behaving correctly.
#
#

## SKELETON STOLEN FROM http://www.bigbold.com/snippets/posts/show/1785
require 'socket'

$SERVER = "127.0.0.1"
$PORT = ARGV[1]  

if ARGV.size == 0
    puts "Usage: ./sircs-tester.rb [servIpAddr] [servPort]"
    puts "Using servIpAddr: #{$SERVER}, servPort: #{$PORT}"
elsif ARGV.size >= 1
    $SERVER = ARGV[0].to_s()
end

if ARGV.size >= 2
begin
    $PORT = Integer(ARGV[1])
rescue
    puts "The port number must be an integer!"
    exit
end
end

puts "Server address - " + $SERVER + ":" + $PORT.to_s()

class IRC

    def initialize(server, port, nick, channel)
        @server = server
        @port = port
        @nick = nick
        @channel = channel
    end

    def recv_data_from_server (timeout)
        pending_event = Time.now.to_i
        received_data = Array.new
        k = 0 
        flag = 0
        while flag == 0
            ## check for timeout
            time_elapsed = Time.now.to_i - pending_event
            if (time_elapsed > timeout)
                flag = 1
            end 
            ready = select([@irc], nil, nil, 0.0001)
            next if !ready
            for s in ready[0]
                if s == @irc then
                    next if @irc.eof
                    s = @irc.gets
                    received_data[k] = s
                    k= k + 1
                end
            end
        end
        return received_data
    end

    def test_silence(timeout)
        data=recv_data_from_server(timeout)
        if (data.size > 0)
            return false
        else
            return true
        end
    end
    
    def send(s)
        # Send a message to the irc server and print it to the screen
        puts "--> #{s}"
        @irc.send "#{s}\n", 0 
    end

    def connect()
        # Connect to the IRC server
        @irc = TCPSocket.open(@server, @port)
    end

    def disconnect()
        @irc.close
    end

    def send_nick(s)
        send("NICK #{s}")
    end
    
    def send_user(s)
        send("USER #{s}")
    end

    def get_motd
        data = recv_data_from_server(1)
        ## CHECK data here
      
        if(data[0] =~ /^:[^ ]+ *375 *rui *:- *[^ ]+ *Message of the day - *.\n/)
            puts "\tRPL_MOTDSTART 375 correct"
        else
            puts "hello"
            puts data[0]
            puts "\tRPL_MOTDSTART 375 incorrect"
            return false
        end
        
        k = 1
        while ( k < data.size-1)
            
            if(data[k] =~ /:[^ ]+ *372 *rui *:- *.*/)
                puts "\tRPL_MOTD 372 correct"
            else
                puts "\tRPL_MOTD 372 incorrect"
                return false
            end
            k = k + 1
        end

        if(data[data.size-1] =~ /:[^ ]+ *376 *rui *:End of \/MOTD command/)
            puts "\tRPL_ENDOFMOTD 376 correct"
        else
            puts "\tRPL_ENDOFMOTD 376 incorrect"
            return false
        end
        
        return true
    end

    def send_privmsg(s1, s2)
	send("PRIVMSG #{s1} :#{s2}")
    end
        
    def raw_join_channel(joiner, channel)
        send("JOIN #{channel}")
        ignore_reply()
    end
    
    def join_channel(joiner, channel)
        send("JOIN #{channel}")
        
        data = recv_data_from_server(1);
        if(data[0] =~ /^:#{joiner}.*JOIN *#{channel}/)
            puts "\tJOIN echoed back"
        else
            puts "\tJOIN was not echoed back to the client"
            return false
        end
        
        if(data[1] =~ /^:[^ ]+ *353 *#{joiner} *= *#{channel} *:.*#{joiner}/)
            puts "\tRPL_NAMREPLY 353 correct"
        else
            puts "\tRPL_NAMREPLY 353 incorrect"
            return false
        end
        
        if(data[2] =~ /^:[^ ]+ *366 *#{joiner} *#{channel} *:End of \/NAMES list/)
            puts "\tRPL_ENDOFNAMES 366 correct"
        else
            puts "\tRPL_ENDOFNAMES 366 incorrect"
            return false
        end
        
        return true
    end

    
    def checkmsg(from, to, msg)
	reply_matches(/^:#{from} *PRIVMSG *#{to} *:#{msg}/, "PRIVMSG")
    end
    
    def check2msg(from, to1, to2, msg)
        data = recv_data_from_server(1);
        if((data[0] =~ /^:#{from} *PRIVMSG *#{to1} *:#{msg}/ && data[1] =~ /^:#{from} *PRIVMSG *#{to2} *:#{msg}/) ||
           (data[1] =~ /^:#{from} *PRIVMSG *#{to1} *:#{msg}/ && data[0] =~ /^:#{from} *PRIVMSG *#{to2} *:#{msg}/))
            puts "\tPRIVMSG to #{to1} and #{to2} correct"
            return true
        else
            puts "\tPRIVMSG to #{to1} and #{to2} incorrect"
            return false
        end
    end
    
    def check_echojoin(from, channel)
	reply_matches(/^:#{from}.*JOIN *#{channel}/,
			    "Test if first client got join echo")
    end
    
    def part_channel(parter, channel)
        send("PART #{channel}")
	reply_matches(/^:#{parter}![^ ]+@[^ ]+ *QUIT *:/)

    end

    def check_part(parter, channel)
	reply_matches(/^:#{parter}![^ ]+@[^ ]+ *QUIT *:/)
    end

    def ignore_reply
        recv_data_from_server(1)
    end

    def reply_matches(rexp, explanation = nil)
	data = recv_data_from_server(1)
	if (rexp =~ data[0])
	    puts "\t #{explanation} correct" if explanation
	    return true
	else
	    puts "\t #{explanation} incorrect: #{data[0]}" if explanation
	    return false
	end
    end

end


##
# The main program.  Tests are listed below this point.  All tests
# should call the "result" function to report if they pass or fail.
##

$total_points = 0

def test_name(n)
    puts "////// #{n} \\\\\\\\\\\\"
    return n
end

def result(n, passed_exp, failed_exp, passed, points)
    explanation = nil
    if (passed)
	print "(+) #{n} passed"
	$total_points += points
	explanation = passed_exp
    else
	print "(-) #{n} failed"
	explanation = failed_exp
    end

    if (explanation)
	puts ": #{explanation}"
    else
	puts ""
    end
end

def eval_test(n, passed_exp, failed_exp, passed, points = 1)
    result(n, passed_exp, failed_exp, passed, points)
    exit(0) if !passed
end

irc = IRC.new($SERVER, $PORT, '', '')
irc.connect()
begin

########## TEST NICK COMMAND ##########################
# The RFC states that there is no response to a NICK command,
# so we test for this.
    tn = test_name("NICK")
    irc.send_nick("sherlock")
    puts "<-- Testing for silence (5 seconds)..."
    
    eval_test(tn, nil, nil, irc.test_silence(5))
    

############## TEST USER COMMAND ##################
# The RFC states that there is no response on a USER,
# so we disconnect first (otherwise the full registration
# of NICK+USER would give us an MOTD), then check
# for silence
    tn = test_name("USER")

    puts "Disconnecting and reconnecting to IRC server\n"
    irc.disconnect()
    irc.connect()

    irc.send_user("myUsername myHostname myServername :My real name")
    puts "<-- Testing for silence (5 seconds)..."

    eval_test(tn, nil, "should not return a response on its own", 
	      irc.test_silence(5))

############# TEST FOR REGISTRATION ##############
# A NICK+USER is a registration that triggers the
# MOTD.  This test sends a nickname to complete the registration,
# and then checks for the MOTD.
    tn = test_name("Registration")
    irc.send_nick("sherlock")
    puts "<-- Listening for MOTD...";
    
    eval_test(tn, nil, nil, irc.get_motd())

############## TEST JOINING ####################
# We join a channel and make sure the client gets
# his join echoed back (which comes first), then
# gets a names list.
    tn = test_name("JOIN")
    eval_test(tn, nil, nil,
	      irc.join_channel("sherlock", "#linux"))




############## PRIVMSG ###################
# Connect a second client that sends a message to the original client.
# Test that the original client receives the message.
    tn = test_name("PRIVMSG")
    irc2 = IRC.new($SERVER, $PORT, '', '')
    irc2.connect()
    irc2.send_nick("john")
    irc2.send_user("myUsername2 myHostname2 myServername2 :My real name 2")
    msg = "2B | !2B, that is the question?"
    irc2.send_privmsg("sherlock", msg)
    eval_test(tn, nil, nil, irc.checkmsg("john", "sherlock", msg))

    # Connect third client who sends msg to 2nd client, 
    # test if second client receives msg
    tn = test_name("PRIVMSG2")
    irc3 = IRC.new($SERVER, $PORT, '', '')
    irc3.connect()
    irc3.send_nick("irene")
    irc2.send_user("myUsername3 myHostname3 myServername3 :My real name 3")
    msg = "What is the purpose of life?"
    irc3.send_privmsg("john", msg)
    eval_test(tn, nil, nil, irc.checkmsg("irene", "john", msg))

############## ECHO JOIN ###################
# When another client joins a channel, all clients
# in that channel should get :newuser JOIN #channel
    tn = test_name("ECHO ON JOIN")
    # "raw" means no testing of responses done
    irc2.raw_join_channel("john", "#linux")
    irc2.ignore_reply()
    eval_test(tn, nil, nil, irc.check_echojoin("john", "#linux"))


############## MULTI-TARGET PRIVMSG ###################
# A client should be able to send a single message to
# multiple targets, with ',' as a delimiter.
# We use client2 to send a message to sherlock and #linux.
# Both should receive the message.
    tn = test_name("MULTI-TARGET PRIVMSG")
    msg = "success is 1 % inspiration and 99 % perspiration"
    irc2.send_privmsg("sherlock, #linux", msg)
    eval_test(tn, nil, nil, irc.check2msg("john", "sherlock", "#linux", msg))
    irc2.ignore_reply()

# Use client3 to send msg to all clients

    tn = test_name("MULTI-TARGET PRIVMSG2")
    msg = "Imagination is more powerful than knowlegdge"
    irc3.send_privmsg("#linux", msg)
    eval_test(tn, nil, nil, irc.check2msg("irene", "#linux", msg))
    irc3.ignore_reply()

# create client 4, send msg to all other clients and channel, check if received
    tn = test_name("MULTI-TARGET PRIVMSG3")
    irc4 = IRC.new($SERVER, $PORT, '', '')
    irc4.connect()
    irc4.send_nick("watson")
    irc4.send_user("myUsername4 myHostname4 myServername4 :My real name 4")
    msg = "It is not okay. It is what it is."
    irc4.send_privmsg("irene", "sherlock", "john", "#linux", msg)
    eval_test(tn, nil, nil, irc.check2msg("irene", "sherlock", "john", "#linux", msg))
    irc4.ignore_reply()

############## PART ###################
# When a client parts a channel, a QUIT message
# is sent to all clients in the channel, including
# the client that is parting.
    tn = test_name("PART")
    eval_test("PART echo to self", nil, nil,
	      irc2.part_channel("john", "#linux"),
	      0) # note that this is a zero-point test!

    eval_test("PART echo to other clients", nil, nil,
	      irc.check_part("john", "#linux"))



# Client 3 disconnects, check if it's automatically parted

    tn = test_name("PART2")
    puts "Disconnecting client 3 - irene...\n"
    irc3.disconnect();
    eval_test("PART echo2 to other clients", nil, nil,
	      irc.check_part("irene", "#linux"))

# 
# Things you might want to test:
#  - Multiple clients in a channel
#  - Abnormal messages of various sorts
#  - Clients that misbehave/disconnect/etc.
#  - Lots and lots of clients
#  - Lots and lots of channel switching
#  - etc.
##


rescue Interrupt
rescue Exception => detail
    puts detail.message()
    print detail.backtrace.join("\n")
ensure
    irc.disconnect()
    puts "Your score: #{$total_points} / 10"
    puts ""
    puts "Good luck with the rest of the project!"
end
