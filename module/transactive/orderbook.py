"""
An orderbook is used to implement a real-time market for a transactive energy system.

A Transactive Energy system may use an Orderbook to implement a continuous market rather 
than a discrete-time double auction [1].  In an Orderbook, two lists of limit orders are 
maintained and market orders are filled in real-time.

Limit orders are used to inform the market of the availability of a resource to supply 
(sell or "ask" order are usually denoted $A$) or consume (buy or "bid" order are usually 
denoted $B$) a specified quantity of service at a specified price for a specified duration.  
In general limit orders are used to announce that a resource is potentially available 
given the right price, but otherwise the resource will not operate.

Market orders are used to obtain from the market an immediate commitment of a resource to 
supply or consume at the best price possible at the present time.  In general market 
orders are used to announce that a resource must operate immediately and will do so at the 
best available price.  The only price guarantee that the resource has is the price will be 
no higher that lowest ask and no lower than the highest bid, given the quantity and 
duration constraints provided.

When a supplier and consumer are matched the resulting cleared orders (denoted $C$) are 
returned to both parties with the additional information pertaining to the amount and cost 
of clearing the orders.

For a supplier and consumer to match, their orders must have the following characteristics, 
which are referred to as the *clearing rules*.

1. The cleared ask must have a price greater than or equal to the submitted ask.

2. The cleared bid must have a price less than or equal to the submitted bid.

3. The intersection of the ask and bid time intervals must be non-null.

4. If the bid is indivisible, the the ask quantity must be greater than or equal to the 
bid quantity, and the bid time interval must be the same as or a sub-interval of the ask 
time interval.

5. If the ask is indivisible, then the bid quantity must be less than or equal to the ask 
quantity, and the ask time interval must be the same as or a sub-interval of the bid time 
interval.

References

[1] Hammerstrom et al., "Pacific Northwest GridWise(TM) Demonstration Testbed: Part I. 
Olympic Peninsula Project", PNNL Report No. 17167, Richland WA, October 2007. 
URL: [https://www.pnnl.gov/main/publications/external/technical_reports/PNNL-17167.pdf]

"""
import matplotlib.pyplot as plt
import json
import datetime
import os
import traceback

if os.path.exists("orderbook_config.py"):
	import orderbook_config as config
else:
	class config:
		warning = True
		debug = False

def print_message(msg,msgtype="MESSAGE",context=None):
	"""Print a message"""
	if context == None:
		context = traceback.extract_stack()[-2].name
	print("%-19.19s [%s %s] %s" % (datetime.datetime.now(),context,msgtype,msg))

def print_warning(msg):
	"""Print a warning message (module.warning must be True)"""
	if config.warning:
		print_message(msg,msgtype="WARNING",context=traceback.extract_stack()[-2].name)

def print_debug(msg):
	"""Print a debug message (module.debug must be True)"""
	if config.debug:
		print_message(msg,msgtype="DEBUG",context=traceback.extract_stack()[-2].name)

class orderbook:
	"""Implementation of orderbook"""
	def __init__(self,unit="MW",time="h",currency="$",price=None):
		self.unit 	= unit
		self.time	= time
		self.currency = currency
		if price == None:
			self.price = "%s/%s.%s" % (currency,unit,time)
			print_debug("price unit not specified, using %s" % (self.price))
		else:
			self.price = price 
		self.reset()

	def reset(self):
		"""Reset the market to initial values"""
		self.buy 	= []
		self.sell 	= []
		self.using = {}
		self.fees = 0.0
		self.settled = []
		print_debug("orderbook initialized")

	def __repr__(self):
		return "<orderbook %s / %s . %s>" % (self.currency, self.unit, self.time)

	def __str__(self):
		return json.dumps({
			"buy"	: self.buy,
			"sell"	: self.sell, 
			"unit"	: self.unit,
			"time"	: self.time,
			"currency": self.currency,
			"price" : self.price,
			"using"	: self.using,
			"fees"	: self.fees,
			"settled": self.settled
			})

	def get_quantityunit(self):
		"""Get the unit of quantity"""
		return self.unit

	def get_timeunit(self):
		"""Get the unit of time"""
		return self.time

	def get_currencyunit(self):
		"""Get the unit of currency"""
		return self.currency

	def get_priceunit(self):
		"""Get the unit of price"""
		return self.price

	def get_settled(self):
		"""Get the settled orders"""
		return self.settled

	def get_buys(self):
		"""Get the pending buy limit orders"""
		return self.buy

	def get_sells(self):
		"""Get the pending sell limit orders"""
		return self.sell

	def get_using(self):
		"""Get the plot using parameters"""
		return self.using

	def submit(self,order):
		"""Submit an order to the market"""
		order.set_market(self)
		if order.islimit():
			if order.issell():
				self.sell.append(order)
				self.sell.sort()
			elif order.isbuy():
				self.buy.append(order)
				self.buy.sort()
			else:
				raise Exception("invalid order type: %s" % order)
			self.clear()
			print_debug("order %s submitted" % order)
			return order
		elif order.ismarket():
			if order.issell():
				return self.find_buy(order)
			else:
				return self.find_sell(order)
		else:
			return None

	def clear(self):
		"""Clear all matching limit orders"""
		# TODO: this may not be correct because indivisible orders can be filled using multiple orders
		#.      the failure is only if the total quantity of limit orders is not sufficient to fill the indivisible order below the bid price
		skip = 0
		while len(self.buy) > skip and len(self.sell) > 0 and self.buy[skip].get_price() >= self.sell[0].get_price():
			trade = min(self.buy[skip].get_quantity(),self.sell[0].get_quantity())
			if not self.buy[skip].isdivisible() and trade < self.buy[skip].get_quantity():
				print_debug("%s cannot buy %g from %s due to indivisible bid" % (repr(self.buy[skip]),trade,repr(self.sell[0])))
				skip += 1
			else:
				print_debug("%s buying %g from %s" % (repr(self.buy[skip]),trade,repr(self.sell[0])))
				self.buy[skip].add_quantity(-trade)
				self.buy[skip].add_amount(trade)
				self.buy[skip].add_value(-self.sell[0].get_price() * trade)
				self.sell[0].add_quantity(-trade)
				self.sell[0].add_amount(trade)
				self.sell[0].add_value(self.buy[skip].get_price() * trade)
				if self.buy[skip].get_quantity() <= 0.0 :
					self.settled.append(self.buy[skip])
					self.buy.remove(self.buy[skip])
				if self.sell[0].get_quantity() <= 0.0 :
					self.settled.append(self.sell[0])
					self.sell.remove(self.sell[0])
			
	def find_buy(self,order):
		"""Find a buy for a sell market order"""
		total = 0.0
		for buy in self.buy:
			if buy.isdivisible() or buy.get_quantity() <= order.get_quantity():
				total += buy.get_quantity()
		if total < order.get_quantity():
			print_debug("%s cannot buy due to insufficient sell depth" % order)
			order.set_cancel()
			return order
		skip = 0
		while len(self.buy) > skip and order.get_quantity() > 0.0:
			trade = min(order.get_quantity(),self.buy[skip].get_quantity())
			if not self.buy[skip].isdivisible() and trade < self.buy[skip].get_quantity():
				print_debug("%s cannot buy %g from %s due to indivisible bid" % (repr(self.buy[skip]),trade,repr(order)))
				skip += 1
			else:
				order.set_price(self.buy[skip].get_price())
				print_debug("%s buying %g from %s" % (repr(order),trade,repr(self.buy[skip])))
				order.add_quantity(-trade)
				order.add_amount(trade)
				order.add_value(-self.buy[skip].get_price() * trade)
				self.buy[skip].add_quantity(-trade)
				self.buy[skip].add_amount(trade)
				self.buy[skip].add_value(order.get_price() * trade)
				if order.get_quantity() <= 0.0 :
					self.settled.append(order)
				if self.buy[skip].get_quantity() <= 0.0 :
					self.settled.append(self.buy[skip])
					self.buy.remove(self.buy[skip])
		return order

	def find_sell(self,order):
		"""Find a sell for a buy market order"""
		if not order.isdivisible():
			total = 0.0
			for sell in self.sell:
				total += sell.get_quantity()
			if total < order.get_quantity():
				print_debug("%s cannot sell due to insufficient buy depth" % order)
				order.set_cancel()
				return order
		while len(self.sell) > 0 and order.get_quantity() > 0.0:
			trade = min(order.get_quantity(),self.sell[0].get_quantity())
			order.set_price(self.sell[0].get_price())
			print_debug("%s buying %g from %s" % (repr(order),trade,repr(self.sell[0])))
			order.add_quantity(-trade)
			order.add_amount(trade)
			order.add_value(-self.sell[0].get_price() * trade)
			self.sell[0].add_quantity(-trade)
			self.sell[0].add_amount(trade)
			self.sell[0].add_value(order.get_price() * trade)
			if order.get_quantity() <= 0.0 :
				self.settled.append(order)
			if self.sell[0].get_quantity() <= 0.0 :
				self.settled.append(self.sell[0])
				self.sell.remove(self.sell[0])
		return order

	def ask(self,quantity,duration,price=None):
		"""Place a sell order"""
		if price is None:
			return self.submit(order(order_type="SELLMARKET",quantity=quantity,duration=duration))
		else:
			return self.submit(order(order_type="SELLLIMIT",quantity=quantity,duration=duration,price=price))

	def bid(self,quantity,duration,price=None):
		"""Plase a buy order"""
		if price is None:
			return self.submit(order(order_type="BUYMARKET",quantity=quantity,duration=duration))
		else:
			return self.submit(order(order_type="BUYLIMIT",quantity=quantity,duration=duration,price=price))

	def divide(self,order):
		"""TODO: divide an order"""
		return None

	def divide_quantity(self,order):
		"""TODO: divide an order by quantity"""
		return None

	def divide_duration(self,order):
		"""TODO: divide an order by duration"""
		return None

	def plot(self,using=None):
		"""Plot the current orderbooks"""
		if using is None:
			using = self.using
		else:
			self.using = using
		fig = plt.figure()
		plt.ylabel("Quantity (%s)" % (self.unit))
		plt.xlabel("Price (%s)" % (self.price))
		sell = []
		q = [0.0]
		p = [0.0]
		for s in self.sell:
			q.append(q[-1])
			q.append(q[-1]+s.get_quantity())
			p.append(s.get_price())
			p.append(s.get_price())
		sell.append(plt.plot(p[1::],q[1::],"r",label="ask"))
		q = [0.0]
		p = [0.0]
		buy = []
		for b in self.buy:
			q.append(q[-1])
			q.append(q[-1]+b.get_quantity())
			p.append(b.get_price())
			p.append(b.get_price())
		buy.append(plt.plot(p[1::],q[1::],"b",label="bid"))
		for item,args in self.using.items():
			if hasattr(plt,item):
				eval("plt.%s(%s)"%(item,args))
		return {"sell":sell,"buy":buy,"figure":fig,"plot":plt} 

class ordertype(str):
	"""Implementation of order type"""
	def __new__(cls, ordertype):
		if ordertype in ["CANCEL","BUYMARKET","SELLMARKET","BUYLIMIT","SELLLIMIT"]:
			return str.__new__(cls, ordertype)
		else:
			raise Exception("ordertype('%s') is not valid" % (ordertype))

	def iscancel(self):
		"""Test if order is cancelled"""
		return self == "CANCEL"

	def ismarket(self):
		"""Test if market order"""
		return self == "BUYMARKET" or self == "SELLMARKET"

	def islimit(self):
		"""Test if limit order"""
		return self == "BUYLIMIT" or self == "SELLLIMIT"

	def isbuy(self):
		"""Test if buy order"""
		return self == "BUYMARKET" or self == "BUYLIMIT"

	def issell(self):
		"""Test if sell order"""
		return self == "SELLMARKET" or self == "SELLLIMIT"

class order(dict):
	"""Implementation of order"""
	next_id = 0

	@classmethod
	def get_next_id(self):
		"""Get the next order id"""
		n = self.next_id
		self.next_id += 1
		return n

	def __init__(self,**kwargs):
		self.market = None
		kwargs["id"] = self.get_next_id()
		if not "order_type" in kwargs.keys():
			raise Exception("'order_type' must be specified")
		if "order_type" in kwargs.keys():
			kwargs["ordertype"] = ordertype(kwargs["order_type"])
			del kwargs["order_type"]
		else:
			kwargs["ordertype"] = ordertype("CANCEL")
		if not "quantity" in kwargs.keys():
			raise Exception("'quantity' must be specified")
		if not "price" in kwargs.keys():
			kwargs["price"] = None
		if not "duration" in kwargs.keys():
			raise Exception("'duration' must be specified")
		kwargs["amount"] = 0.0
		kwargs["value"] = 0.0
		if not "divisible" in kwargs.keys():
			if kwargs["ordertype"].isbuy():
				kwargs["divisible"] = False
			else:
				kwargs["divisible"] = True
		dict.__init__(self,**kwargs)

	def __repr__(self):
		result = "<order:%d %s" % (self.get_id(),self.get_ordertype())
		if self.get_quantity() > 0.0:
			if not self.isdivisible():
				result += " INDIVISIBLE"
			if self.get_price() == None:
				result += " %s%s for %s%s" % (self.get_quantity(),self.get_unit(),
											  self.get_duration(),self.get_time())
			else:
				result += " %s%s at %s%s for %s%s" % (self.get_quantity(),self.get_unit(),
													self.get_price(),self.get_priceunit(),
													self.get_duration(),self.get_time())
		if self.get_amount() != 0.0:
			result += " FILLED %s%s%s for %s%s" % (self.get_amount(),self.get_unit(),self.get_time(),
												 self.get_value(),self.get_currency())
		result += ">"
		return result

	def iscancel(self):
		"""Test if order is cancelled"""
		return self["ordertype"].iscancel()

	def ismarket(self):
		"""Test if market order"""
		return self["ordertype"].ismarket()

	def islimit(self):
		"""Test if limit order"""
		return self["ordertype"].islimit()

	def isbuy(self):
		"""Test if buy order"""
		return self["ordertype"].isbuy()

	def issell(self):
		"""Test if sell order"""
		return self["ordertype"].issell()

	def get_ordertype(self):
		""" Get order type"""
		return self["ordertype"]

	def get_id(self):
		"""Get order id"""
		return self["id"]

	def get_price(self):
		"""Get order price"""
		return self["price"]

	def get_quantity(self):
		"""Get order quantity"""
		return self["quantity"]

	def get_duration(self):
		"""Get order duration"""
		return self["duration"]

	def get_amount(self):
		"""Get filled order amount"""
		return self["amount"]

	def get_value(self):
		"""Get filled order value"""
		return self["value"]

	def set_cancel(self):
		"""Cancel order"""
		self["ordertype"] = ordertype("CANCEL")

	def set_price(self,x):
		"""Set order price"""
		self["price"] = x

	def set_quantity(self,x):
		"""Set order quantity"""
		self["quantity"] = x

	def add_quantity(self,x):
		"""Add to order quantity"""
		self["quantity"] += x

	def add_amount(self,x):
		"""Add to order amount"""
		self["amount"] += x

	def add_value(self,x):
		"""Add to order value"""
		self["value"] += x

	def isdivisible(self):
		"""Get status of order divisibility"""
		return self["divisible"]

	def set_market(self,market):
		"""Set the market in which this order is placed"""
		self.market = market

	def get_unit(self):
		"""Get the unit of the order"""
		if self.market:
			return self.market.unit
		else:
			return ""

	def get_time(self):
		"""Get the time duration unit of order"""
		if self.market:
			return self.market.time
		else:
			return ""

	def get_currency(self):
		"""Get the currency of the order"""
		if self.market:
			return self.market.currency
		else:
			return ""

	def get_priceunit(self):
		"""Get the price unit of the order"""
		if self.market:
			return self.market.price
		else:
			return ""

	def __lt__(self,a):
		if self["ordertype"].isbuy() and a["ordertype"].isbuy():
			return self["price"] > a["price"]
		elif self["ordertype"].issell() and a["ordertype"].issell():
			return self["price"] < a["price"]
		else:
			raise Exception("cannot compare %s order to %s order" % (self["ordertype"],a["ordertype"]))

def selftest():
	"""Runs the complete module self-test"""

	# check new orderbook structure
	market = orderbook()
	assert( market.get_quantityunit() == "MW" )
	assert( market.get_timeunit() == "h" )
	assert( market.get_currencyunit() == "$" )
	assert( market.get_priceunit() == "$/MW.h")
	assert( market.get_buys() == [] )
	assert( market.get_sells() == [] )
	assert( market.get_settled() == [] )

	# check plot result
	p = market.plot(using={"grid":"", "legend":"", "savefig":"'selftest.png'"})
	assert( p["figure"].__class__.__name__ == "Figure" )
	assert( p["sell"][0][0].__class__.__name__ == "Line2D" )
	assert( p["buy"][0][0].__class__.__name__ == "Line2D" )
	assert( p["plot"] == plt )

	# simple order match
	b = market.bid(quantity=1.0, duration=1.0, price=1.0)
	s = market.ask(quantity=1.0, duration=1.0, price=1.0)
	assert( market.get_buys() == [] )
	assert( market.get_sells() == [] )

if __name__ == '__main__':
 	selftest()
