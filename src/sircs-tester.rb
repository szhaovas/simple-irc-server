#! /usr/bin/env ruby
#
# CMPU-375 Simple IRC server tester
#
# This script sends a sequence of commands to your IRC server and inspects
# the replies to see if it is behaving correctly.
#
# Some tests depend on the correct execution of previous ones.
# For example, you cannot pass the "PART" test if your "JOIN" test failed.
# For this reason, the script exits once the server fails a test.
# If you fail a specific test, we suggest you modify this code
# to print out exactly # what is going on, or perhaps remove tests that
# fail so that you can test other, unrelated tests if you simply haven't
# implemented a particular bit of functionality yet (e.g., you might
# remove the WHO or LIST tests to test PART).
#
# Part of the reason we give out the code for this
# script so that you can modify it and write your own test
# scripts later on.  Remember that passing this does not ensure full credit
# on the final IRC server grade:  The final tests will be more extensive
# so you may wish to add your own, more stressful tests to this script, for
# completeness.
#
# Enjoy!
#

## SKELETON STOLEN FROM http://www.bigbold.com/snippets/posts/show/1785
require 'socket'

$SERVER = "127.0.0.1"
$PORT = 6666  ########## DONT FORGET TO CHANGE THIS

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

    def send_raw(s)
        # Send a message to the irc server and print it to the screen
        puts "--> #{s}"
        @irc.send "#{s}", 0
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

    def who(s)
        send("WHO #{s}")

        data = recv_data_from_server(1);

        if(data[0] =~ /^:[^ ]+ *352 *rui *#{s} *myUsername *[^ ]+ *[^ ]+ *rui *H *:0 */) #fix---------------------------------------------------------------------
            puts "\tRPL_WHOREPLY 352 correct"
        else
            puts data
            puts "\tRPL_WHOREPLY 352 incorrect"
            return false
        end

        if(data[1] =~ /^:[^ ]+ *315 *rui *#{s} *:End of \/WHO list/)
            puts "\tRPL_ENDOFWHO 315 correct"
        else
            puts "\tRPL_ENDOFWHO 315 incorrect"
            return false
        end
        return true
    end

    def list
	send("LIST")

        data = recv_data_from_server(1);
        if(data[0] =~ /^:[^ ]+ *321 *rui *Channel *:Users Name/)
            puts "\tRPL_LISTSTART 321 correct"
        else
            puts "\tRPL_LISTSTART 321 incorrect"
            return false
        end


        if(data[1] =~ /^:[^ ]+ *322 *rui *#linux *1/) #fix---------------------------------------------------------------------------------------------------------
            puts "\tRPL_LIST 322 correct"
        else
            puts "\tRPL_LIST 322 incorrect"
            return false
        end

        if(data[2] =~ /^:[^ ]+ *323 *rui *:End of \/LIST/)
            puts "\tRPL_LISTEND 323 correct"
        else
            puts data[2]
            puts "\tRPL_LISTEND 323 incorrect"
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
	reply_matches(/^:#{parter}![^ ]+@[^ ]+ *QUIT *:/) #fix----------------------------------------------------------------

    end

    def check_part(parter, channel)
	reply_matches(/^:#{parter}![^ ]+@[^ ]+ *QUIT *:/) #fix----------------------------------------------------------------
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

  def invalid_nick(s)
      send("NICK #{s}")

      data = recv_data_from_server(1);

      ss = s[0,9]

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *432 \* *#{ss} *:Erroneus nickname */)
          return true
      else
          puts data
          puts "NICK " +s+ " should return ERR_ERRONEUSNICKNAME and nothing more"
          return false
      end
  end

  def used_nick(s)
      send("NICK #{s}")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *433 *rui *#{s} *:Nickname is already in use */)
          return true
      else
          puts data
          puts "NICK " +s+ " should return ERR_NICKNAMEINUSE and nothing more"
          return false
      end
  end

  def no_nick()
      send("NICK")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *431 *rui *:No nickname given */)
          return true
      else
          puts data
          puts "NICK <blank> should return ERR_NONICKNAMEGIVEN and nothing more"
          return false
      end
  end

  def less_params(cmd)
      send("#{cmd}")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *461 *rui #{cmd} *:Not enough parameters */)
          return true
      else
          puts data
          puts "#{cmd} should return ERR_NEEDMOREPARAMS and nothing more"
          return false
      end
  end

  def reset_user()
      send("USER a a a a")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *462 *rui *:You may not reregister */)
          return true
      else
          puts data
          puts "Resetting user info after registration should return ERR_ALREADYREGISTRED and nothing more"
          return false
      end
  end

  def invalid_chan(cmd, s)
      send("#{cmd} #{s}")

      data = recv_data_from_server(1);

      ss = s[0,9]

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *403 *rui *#{ss} *:No such channel */)
          return true
      else
          puts data
          puts "#{cmd} #{s} should return ERR_NOSUCHCHANNEL and nothing more"
          return false
      end
  end

  def not_on_chan(s)
      send("PART #{s}")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *442 *rui2 *#{s} *:You're not on that channel */)
          return true
      else
          puts data
          puts "PART #{s} should return ERR_NOTONCHANNEL and nothing more"
          return false
      end
  end

  def no_recipient()
      send("PRIVMSG")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *411 *rui *:No recipient given \(PRIVMSG\) */)
          return true
      else
          puts data
          puts "PRIVMSG given no param should return ERR_NORECIPIENT and nothing more"
          return false
      end
  end

  def no_text()
      send("PRIVMSG rui2")

      data = recv_data_from_server(1);

      if(data.size == 1 and data[0] =~ /^:[^ ]+ *412 *rui *:No text to send */)
          return true
      else
          puts data
          puts "PRIVMSG given one param should return ERR_NOTEXTTOSEND and nothing more"
          return false
      end
  end

  def nonexistent_target(s)
      ls = s[0]
      i = 1
      while i < s.length
          ls += ",#{s[i]}"
          i += 1
      end

      send("PRIVMSG #{ls} hi")

      data = recv_data_from_server(1);

      i = 0
      while i < s.length
          if (not data[i] =~ /^:[^ ]+ *401 *rui *#{s[i]} *:No such nick\/channel */)
              puts data
              puts "PRIVMSG should return ERR_NOSUCHNICK for each of the non-existent targets and nothing more"
              return false
          end
          i += 1
      end

      return true
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



begin

########### TEST MESSAGE SPLITTING ##########################
## Please enable DEBUG_SPLIT and examine server debug messages

    tn = test_name("Split")
    irc = IRC.new($SERVER, $PORT, '', '')
    irc.connect()

    # Overflow an empty buffer
    irc.send_raw("0" * 512) # should throw away this and keep throwing
    # Empty buffer, keep throwing + a good msg
    irc.send_raw("1\nnick a\n") # should extract "nick a"

    # Overflow an empty buffer. Should throw away everything.
    irc.send_raw("0" * 2049 + "0\n")

    # Empty buffer, multiple incomplete good msg "nick a" and "user a a a :"
    irc.send_raw("ni")
    irc.send_raw("ck")
    irc.send_raw(" a\nuser a a a:")
    irc.send_raw("\n")

     # Fill the buffer with trash
    irc.send_raw("0"*511)
    irc.send_raw("nick a\n") # This is not a good msg because it is part of a illegal one
    irc.disconnect()
    irc.connect()

########## TEST NICK COMMAND ##########################
# The RFC states that there is no response to a NICK command,
# so we test for this.
   tn = test_name("NICK")
   irc.send_nick("rui")
   puts "<-- Testing for silence (1 seconds)..."

   eval_test(tn, nil, nil, irc.test_silence(1))


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
   puts "<-- Testing for silence (1 seconds)..."

   eval_test(tn, nil, "should not return a response on its own",
         irc.test_silence(1))

############# TEST FOR REGISTRATION ##############
# A NICK+USER is a registration that triggers the
# MOTD.  This test sends a nickname to complete the registration,
# and then checks for the MOTD.
   tn = test_name("Registration")
   irc.send_nick("rui")
   puts "<-- Listening for MOTD...";

   eval_test(tn, nil, nil, irc.get_motd())

############## TEST JOINING ####################
# We join a channel and make sure the client gets
# his join echoed back (which comes first), then
# gets a names list.
   tn = test_name("JOIN")
   eval_test(tn, nil, nil,
         irc.join_channel("rui", "#linux"))

############## WHO ####################
# Who should list everyone in a channel
# or everyone on the server.  We are only
# checking WHO <channel>.
# The response should follow the RFC.
   tn = test_name("WHO")
   eval_test(tn, nil, nil, irc.who("#linux"))

############## LIST ####################
# LIST is used to check all the channels on a server.
# The response should include #linux and its format should follow the RFC.
    tn = test_name("LIST")
    eval_test(tn, nil, nil, irc.list())

############## PRIVMSG ###################
# Connect a second client that sends a message to the original client.
# Test that the original client receives the message.
   tn = test_name("PRIVMSG")
   irc2 = IRC.new($SERVER, $PORT, '', '')
   irc2.connect()
   irc2.send_nick("rui2")
   irc2.send_user("myUsername2 myHostname2 myServername2 :My real name 2")
   msg = "to IRC or not to IRC, that is the question"
   irc2.send_privmsg("rui", msg)
   eval_test(tn, nil, nil, irc.checkmsg("rui2", "rui", msg))

############## ECHO JOIN ###################
# When another client joins a channel, all clients
# in that channel should get :newuser JOIN #channel
   tn = test_name("ECHO ON JOIN")
   # "raw" means no testing of responses done
   irc2.raw_join_channel("rui2", "#linux")
   irc2.ignore_reply()
   eval_test(tn, nil, nil, irc.check_echojoin("rui2", "#linux"))


############## MULTI-TARGET PRIVMSG ###################
# A client should be able to send a single message to
# multiple targets, with ',' as a delimiter.
# We use client2 to send a message to rui and #linux.
# Both should receive the message.
   tn = test_name("MULTI-TARGET PRIVMSG")
   msg = "success is 1 pcent inspiration and 99 pcent perspiration"
   irc2.send_privmsg("rui,#linux", msg)
   eval_test(tn, nil, nil, irc.check2msg("rui2", "rui", "#linux", msg))
   irc2.ignore_reply()

############## PART ###################
# When a client parts a channel, a QUIT message
# is sent to all clients in the channel, including
# the client that is parting.
   tn = test_name("PART")
   eval_test("PART echo to self", nil, nil,
         irc2.part_channel("rui2", "#linux"),
         0) # note that this is a zero-point test!

   eval_test("PART echo to other clients", nil, nil,
         irc.check_part("rui2", "#linux"))

## Your tests go here!

############## NICK_ERRORS ###################
# INVALID_NICKNAME
# <nick> ::= <letter> { <letter> | <number> | <special> }
# <nick> cannot consist of more than 9 characters

   tn = test_name("SPEC_CHAR_NICKNAME")
   irc3 = IRC.new($SERVER, $PORT, '', '')
   irc3.connect()
   eval_test(tn, nil, nil, irc3.invalid_nick("#Datroll"))

   tn = test_name("LONG_NICKNAME")
   eval_test(tn, nil, nil, irc3.invalid_nick("lololololololololololololololo"))
   irc3.disconnect()

# USED_NICKNAME
# NICK <used_nickname> should return ERR_NICKNAMEINUSE

    tn = test_name("USED_NICKNAME")
    eval_test(tn, nil, nil, irc.used_nick("rui2"))

# NO_NICK_NAME_GIVEN
# NICK <blank> should return ERR_NONICKNAMEGIVEN

    tn = test_name("NO_NICK_NAME_GIVEN")
    eval_test(tn, nil, nil, irc.no_nick())

############## USER_ERRORS ###################
# NOT_ENOUGH_PARAMETERS
# USER <less than 4 params> should return ERR_NEEDMOREPARAMS

    tn = test_name("USER_NOT_ENOUGH_PARAMETERS")
    eval_test(tn, nil, nil, irc.less_params("USER"))

# RESET_USER_AFTER_REGISTRATION
# Client cannot reset user information after registered

    tn = test_name("RESET_USER_AFTER_REGISTRATION")
    eval_test(tn, nil, nil, irc.reset_user())

############## JOIN_ERRORS ###################
# NOT_ENOUGH_PARAMETERS
# JOIN <blank> should return ERR_NEEDMOREPARAMS

    tn = test_name("JOIN_NOT_ENOUGH_PARAMETERS")
    eval_test(tn, nil, nil, irc.less_params("JOIN"))

# INVALID_CHANNAME
# <channel> ::= ('#' | '&') <chstring>
# <channel> cannot consist of more than 9 characters

   tn = test_name("JOIN_NO_LEADING_#&")
   eval_test(tn, nil, nil, irc.invalid_chan("JOIN", "channel"))

   tn = test_name("JOIN_LONG_CHANNAME")
   eval_test(tn, nil, nil, irc.invalid_chan("JOIN", "#lololololololololololololololo"))

############## PART_ERRORS ###################
# NOT_ENOUGH_PARAMETERS
# PART <blank> should return ERR_NEEDMOREPARAMS

    tn = test_name("PART_NOT_ENOUGH_PARAMETERS")
    eval_test(tn, nil, nil, irc.less_params("PART"))

# INVALID_CHANNAME
# <channel> ::= ('#' | '&') <chstring>
# <channel> cannot consist of more than 9 characters
# should also return ERR_NOSUCHCHANNEL if channame is valid but chan hasn't been created

   tn = test_name("PART_NO_LEADING_#&")
   eval_test(tn, nil, nil, irc.invalid_chan("PART", "channel"))

   tn = test_name("PART_LONG_CHANNAME")
   eval_test(tn, nil, nil, irc.invalid_chan("PART", "#lololololololololololololololo"))

   tn = test_name("NONEXISTENT_CHAN")
   eval_test(tn, nil, nil, irc.invalid_chan("PART", "#nosuchchan"))

# NOT_ON_CHANNEL
# A client cannot part from a channel if it is not on the channel

   tn = test_name("NOT_ON_CHANNEL")
   eval_test(tn, nil, nil, irc2.not_on_chan("#linux"))

############## PRIVMSG_ERRORS ###################
# GIVE_NO_PARAM
# Since there is no way to distinguish between target and text_to_send, we choose
#     to return ERR_NORECIPIENT when no param is given, and ERR_NOTEXTTOSEND when
#     1 param is given.

    tn = test_name("GIVE_NO_PARAM")
    eval_test(tn, nil, nil, irc.no_recipient())

# GIVE_ONE_PARAM
# See GIVE_NO_PARAM

    tn = test_name("GIVE_ONE_PARAM")
    eval_test(tn, nil, nil, irc.no_text())

# SEND_TO_NONEXISTENT_TARGET

    targets = ["rui4", "#linux2"]
    tn = test_name("SEND_TO_NONEXISTENT_TARGET")
    eval_test(tn, nil, nil, irc.nonexistent_target(targets))

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
